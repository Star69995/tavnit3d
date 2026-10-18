#include "object_parts.hpp"
#include "object_dimensions.hpp"
#include "body_solids.hpp"
#include "document.hpp"
#include "hollow.hpp"
#include "round_edges.hpp"
#include "group/group.hpp"
#include "group/group_fillet.hpp"
#include "group/group_shell.hpp"
#include "group/group_solid_model_operation.hpp"
#include "entity/entity.hpp"
#include "entity/entity_workplane.hpp"
#include "constraint/constraint_point_distance_hv.hpp"
#include "constraint/iconstraint_datum.hpp"
#include "solid_model/solid_model_occ.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <ranges>
#include <set>
#include <sstream>

namespace dune3d {

namespace {
std::vector<const Group *> groups_of_body(const Document &doc, const UUID &body)
{
    for (const auto &bg : doc.get_groups_by_body()) {
        if (bg.get_group().m_uuid == body)
            return bg.groups;
    }
    return {};
}

void collect_parts(const Document &doc, const UUID &body, std::vector<UUID> &parts, std::set<UUID> &seen)
{
    if (!seen.insert(body).second)
        return;
    bool combined = false;
    for (auto gr : groups_of_body(doc, body)) {
        auto op = dynamic_cast<const GroupSolidModelOperation *>(gr);
        if (!op)
            continue;
        combined = true;
        for (const auto &src : {op->m_source_group_argument, op->m_source_group_tool}) {
            if (!doc.get_groups().contains(src))
                continue;
            collect_parts(doc, doc.get_group(src).find_body(doc).group.m_uuid, parts, seen);
        }
    }
    if (!combined && std::ranges::find(parts, body) == parts.end())
        parts.push_back(body);
}

// what has to be the same for parts to be alike; where they are doesn't count
std::string part_signature(const Document &doc, const UUID &body)
{
    std::ostringstream s;
    for (auto gr : groups_of_body(doc, body))
        s << static_cast<int>(gr->get_type()) << ";";
    for (const auto &d : get_object_dimensions(doc, body)) {
        if (d.label == "X" || d.label == "Y")
            continue;
        s << d.label << "=" << std::llround(d.value * 1000) << ";";
    }
    // the finders don't change anything
    auto &mdoc = const_cast<Document &>(doc);
    if (auto f = find_round_edges(mdoc, body))
        s << "r" << std::llround(f->m_radius * 1000) << ";";
    if (auto h = find_hollow(mdoc, body))
        s << "h" << std::llround(h->m_thickness * 1000) << static_cast<int>(h->m_open) << ";";
    return s.str();
}

// "Leg 1", "Leg 2" -> "Leg"
std::string common_name(const std::vector<std::string> &names)
{
    if (names.empty())
        return "";
    if (std::ranges::all_of(names, [&names](const auto &n) { return n == names.front(); }))
        return names.front();
    auto stem = [](std::string n) {
        while (n.size() && (std::isdigit(static_cast<unsigned char>(n.back())) || n.back() == ' '))
            n.pop_back();
        return n;
    };
    const auto first = stem(names.front());
    if (first.size() && std::ranges::all_of(names, [&](const auto &n) { return stem(n) == first; }))
        return first;
    return names.front();
}

bool is_upright(const glm::dvec3 &ax, const glm::dvec3 &ay)
{
    return glm::length(ax - glm::dvec3(1, 0, 0)) < 1e-6 && glm::length(ay - glm::dvec3(0, 1, 0)) < 1e-6;
}

bool overlap(double lo1, double hi1, double lo2, double hi2, double tol)
{
    return lo1 < hi2 + tol && hi1 > lo2 - tol;
}

// how a part's middle follows a side that moves: near an edge, with that edge (legs stay in
// the corners of a top); in between, in proportion
double follow_along(double c, double lo, double hi, double dlo, double dhi)
{
    if (hi - lo < 1e-9 || c < lo || c > hi)
        return 0;
    const double t = (c - lo) / (hi - lo);
    if (t < .25)
        return dlo;
    if (t > .75)
        return dhi;
    return dlo + t * (dhi - dlo);
}
} // namespace

std::vector<UUID> get_object_parts(const Document &doc, const UUID &body)
{
    std::vector<UUID> parts;
    std::set<UUID> seen;
    collect_parts(doc, body, parts, seen);
    if (parts.size() == 1 && parts.front() == body)
        return {};
    return parts;
}

std::vector<PartSet> group_alike_parts(const Document &doc, const std::vector<UUID> &parts)
{
    std::vector<std::pair<std::string, PartSet>> sets;
    for (const auto &part : parts) {
        const auto sig = part_signature(doc, part);
        auto it = std::ranges::find_if(sets, [&sig](const auto &s) { return s.first == sig; });
        if (it == sets.end())
            sets.push_back({sig, PartSet{"", {part}}});
        else
            it->second.parts.push_back(part);
    }
    std::vector<PartSet> r;
    for (auto &[sig, set] : sets) {
        std::vector<std::string> names;
        for (const auto &p : set.parts)
            names.push_back(doc.get_group(p).m_body->m_name);
        set.name = common_name(names);
        r.push_back(std::move(set));
    }
    return r;
}

std::optional<PartPosition> get_part_position(const Document &doc, const UUID &body)
{
    if (!doc.get_groups().contains(body))
        return {};
    const auto &body_group = doc.get_group(body);
    PartPosition pos;
    pos.body = body;
    // a workplane of its own, made in the body's first group
    if (auto wp = doc.get_entity_ptr<EntityWorkplane>(body_group.m_active_wrkpl); wp && wp->m_group == body) {
        pos.group = body;
        pos.wrkpl = wp->m_uuid;
        pos.origin = wp->m_origin;
        pos.ax = wp->transform_relative({1, 0});
        pos.ay = wp->transform_relative({0, 1});
        return pos;
    }
    // X/Y dimensions from a workplane's origin, in the first group that has them
    for (auto gr : groups_of_body(doc, body)) {
        UUID wrkpl;
        for (auto &[uu, co] : doc.m_constraints) {
            if (co->m_group != gr->m_uuid)
                continue;
            auto hv = dynamic_cast<const ConstraintPointDistanceHV *>(co.get());
            if (!hv || hv->is_measurement())
                continue;
            auto en = doc.get_entity_ptr(hv->m_entity1.entity);
            if (!en || en->get_type() != Entity::Type::WORKPLANE)
                continue;
            wrkpl = hv->m_wrkpl;
            if (co->get_type() == Constraint::Type::POINT_DISTANCE_HORIZONTAL) {
                pos.cx = uu;
                pos.xy.x = hv->m_distance;
            }
            else {
                pos.cy = uu;
                pos.xy.y = hv->m_distance;
            }
        }
        if (pos.cx && pos.cy) {
            auto wp = doc.get_entity_ptr<EntityWorkplane>(wrkpl);
            if (!wp)
                return {};
            pos.group = gr->m_uuid;
            pos.ax = wp->transform_relative({1, 0});
            pos.ay = wp->transform_relative({0, 1});
            return pos;
        }
        pos.cx = pos.cy = UUID();
    }
    return {};
}

void set_part_position(Document &doc, const PartPosition &pos, const glm::dvec3 &delta)
{
    if (pos.wrkpl) {
        auto wp = doc.get_entity_ptr<EntityWorkplane>(pos.wrkpl);
        if (!wp)
            return;
        wp->m_origin = pos.origin + delta;
        // helper planes inside the body (upright / flat ones of revolves and outlines) are tied
        // to its sketch, but the solver would only meet halfway: move them along first
        for (auto gr : groups_of_body(doc, pos.body)) {
            for (auto &[uu, en] : doc.m_entities) {
                if (en->m_group == gr->m_uuid && en->m_kind == ItemKind::USER && uu != pos.wrkpl
                    && en->get_type() == Entity::Type::WORKPLANE)
                    static_cast<EntityWorkplane &>(*en).m_origin += delta;
            }
        }
        // its extrusion's far side would otherwise start out where it was, and the solver could
        // flip it over to stay near there: make it again from the moved sketch first
        doc.set_group_generate_pending(pos.group);
    }
    else {
        auto cx = dynamic_cast<ConstraintPointDistanceHV *>(doc.get_constraint_ptr(pos.cx));
        auto cy = dynamic_cast<ConstraintPointDistanceHV *>(doc.get_constraint_ptr(pos.cy));
        if (!cx || !cy)
            return;
        cx->m_distance = pos.xy.x + glm::dot(delta, pos.ax);
        cy->m_distance = pos.xy.y + glm::dot(delta, pos.ay);
    }
    doc.set_group_solve_pending(pos.group);
}

std::optional<Bounds> get_body_bounds(const Document &doc, const UUID &body)
{
    const auto solids = get_body_solids(doc, UUID(), [&body](const UUID &b) { return b == body; });
    if (solids.empty())
        return {};
    Bounds b{glm::dvec3(std::numeric_limits<double>::infinity()),
             glm::dvec3(-std::numeric_limits<double>::infinity())};
    for (const auto &face : solids.front().solid->m_faces) {
        for (const auto &v : face.vertices) {
            b.lo = glm::min(b.lo, glm::dvec3(v.x, v.y, v.z));
            b.hi = glm::max(b.hi, glm::dvec3(v.x, v.y, v.z));
        }
    }
    if (b.lo.x > b.hi.x)
        return {};
    return b;
}

SideChange get_side_change(const std::string &label, double old_value, double new_value, const Bounds &bounds)
{
    const double d = new_value - old_value;
    SideChange c;
    // the ready-made shapes are centred on their position and stand on their workplane
    if (label == "Height") {
        c.hi.z = d;
    }
    else if (label == "Width") {
        c.lo.x = -d / 2;
        c.hi.x = d / 2;
    }
    else if (label == "Depth") {
        c.lo.y = -d / 2;
        c.hi.y = d / 2;
    }
    else if (label == "Diameter" || label == "Outer diameter") {
        c.lo.x = c.lo.y = -d / 2;
        c.hi.x = c.hi.y = d / 2;
        // a sphere grows upwards too
        if (std::abs((bounds.hi.z - bounds.lo.z) - old_value) < 1e-3 * old_value + 1e-3)
            c.hi.z = d;
    }
    else if (label == "Radius") {
        c.lo.x = c.lo.y = -d;
        c.hi.x = c.hi.y = d;
    }
    return c;
}

PartsFollow begin_parts_follow(const Document &doc, const std::vector<UUID> &parts, const std::vector<UUID> &changed)
{
    PartsFollow f;
    f.changed = changed;
    f.usable = true;
    for (const auto &p : parts) {
        if (auto b = get_body_bounds(doc, p))
            f.bounds.emplace(p, *b);
        if (auto pos = get_part_position(doc, p))
            f.positions.emplace(p, *pos);
    }
    if (changed.size())
        f.dims = get_object_dimensions(doc, changed.front());
    for (const auto &p : changed) {
        auto it = f.positions.find(p);
        if (!f.bounds.contains(p) || it == f.positions.end() || !is_upright(it->second.ax, it->second.ay))
            f.usable = false;
    }
    return f;
}

void follow_size_change(Document &doc, const PartsFollow &f, const SideChange &change)
{
    if (!f.usable)
        return;
    auto is_changed = [&f](const UUID &p) { return std::ranges::find(f.changed, p) != f.changed.end(); };
    // touching: within a bit, more for bigger things (recipes often let parts sink into each other)
    auto tol_for = [](double a, double b) { return 1 + .1 * std::min(a, b); };
    auto xy_overlap = [](const Bounds &a, const Bounds &b) {
        return overlap(a.lo.x, a.hi.x, b.lo.x, b.hi.x, 0) && overlap(a.lo.y, a.hi.y, b.lo.y, b.hi.y, 0);
    };
    std::map<UUID, glm::dvec3> deltas;
    std::deque<UUID> moved_up;
    for (const auto &p : f.changed) {
        const auto &b = f.bounds.at(p);
        for (const auto &[q, qb] : f.bounds) {
            if (is_changed(q) || deltas.contains(q) || !f.positions.contains(q))
                continue;
            glm::dvec3 d{0, 0, 0};
            bool touching = false;
            const double tz = tol_for(qb.hi.z - qb.lo.z, b.hi.z - b.lo.z);
            const bool above = xy_overlap(qb, b) && std::abs(qb.lo.z - b.hi.z) <= tz;
            const bool below = xy_overlap(qb, b) && std::abs(qb.hi.z - b.lo.z) <= tz;
            if (above || below) {
                touching = true;
                d.z = above ? change.hi.z : change.lo.z;
                const auto c = (qb.lo + qb.hi) / 2.;
                d.x = follow_along(c.x, b.lo.x, b.hi.x, change.lo.x, change.hi.x);
                d.y = follow_along(c.y, b.lo.y, b.hi.y, change.lo.y, change.hi.y);
            }
            else if (overlap(qb.lo.z, qb.hi.z, b.lo.z, b.hi.z, 0)) {
                // beside it
                const double tx = tol_for(qb.hi.x - qb.lo.x, b.hi.x - b.lo.x);
                const double ty = tol_for(qb.hi.y - qb.lo.y, b.hi.y - b.lo.y);
                if (overlap(qb.lo.y, qb.hi.y, b.lo.y, b.hi.y, 0)) {
                    if (std::abs(qb.lo.x - b.hi.x) <= tx) {
                        d.x = change.hi.x;
                        touching = true;
                    }
                    else if (std::abs(qb.hi.x - b.lo.x) <= tx) {
                        d.x = change.lo.x;
                        touching = true;
                    }
                }
                if (!touching && overlap(qb.lo.x, qb.hi.x, b.lo.x, b.hi.x, 0)) {
                    if (std::abs(qb.lo.y - b.hi.y) <= ty) {
                        d.y = change.hi.y;
                        touching = true;
                    }
                    else if (std::abs(qb.hi.y - b.lo.y) <= ty) {
                        d.y = change.lo.y;
                        touching = true;
                    }
                }
            }
            if (!touching)
                continue;
            deltas.emplace(q, d);
            moved_up.push_back(q);
        }
    }
    // what stands on a part that moved goes along with it
    while (moved_up.size()) {
        const auto q = moved_up.front();
        moved_up.pop_front();
        const auto &qb = f.bounds.at(q);
        for (const auto &[r, rb] : f.bounds) {
            if (is_changed(r) || deltas.contains(r) || !f.positions.contains(r))
                continue;
            if (xy_overlap(rb, qb) && std::abs(rb.lo.z - qb.hi.z) <= tol_for(rb.hi.z - rb.lo.z, qb.hi.z - qb.lo.z)) {
                deltas.emplace(r, deltas.at(q));
                moved_up.push_back(r);
            }
        }
    }
    for (const auto &[q, d] : deltas)
        set_part_position(doc, f.positions.at(q), d);
}

void set_parts_dimension(Document &doc, const PartsFollow &f, size_t index, double value)
{
    if (index >= f.dims.size())
        return;
    const auto &dim = f.dims.at(index);
    for (const auto &p : f.changed) {
        const auto dims = get_object_dimensions(doc, p);
        if (index >= dims.size() || dims.at(index).label != dim.label)
            continue;
        auto datum = dynamic_cast<IConstraintDatum *>(doc.get_constraint_ptr(dims.at(index).constraint));
        if (!datum)
            continue;
        datum->set_datum(value / dims.at(index).scale);
        doc.set_group_solve_pending(dims.at(index).group);
    }
    if (dim.label == "X" || dim.label == "Y" || !f.bounds.contains(f.changed.front()))
        return;
    follow_size_change(doc, f, get_side_change(dim.label, dim.value, value, f.bounds.at(f.changed.front())));
}

} // namespace dune3d

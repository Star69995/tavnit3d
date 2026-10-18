#include "duplicate.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/entity/entity.hpp"
#include "document/constraint/constraint.hpp"
#include "document/constraint/constraint_point_distance_hv.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_lock_rotation.hpp"
#include "document/group/group_sweep.hpp"
#include "nlohmann/json.hpp"
#include <map>
#include <ranges>

namespace dune3d {

namespace {
// replace every string in j that names an old item with the new one
void remap(json &j, const std::map<std::string, std::string> &map)
{
    if (j.is_string()) {
        if (auto it = map.find(j.get<std::string>()); it != map.end())
            j = it->second;
    }
    else if (j.is_object() || j.is_array()) {
        for (auto &el : j)
            remap(el, map);
    }
}

bool same_geometry(const Document &doc, const Entity &a, const Entity &b)
{
    if (a.get_type() != b.get_type())
        return false;
    for (unsigned int pt = 0; pt < 8; pt++) {
        if (a.is_valid_point(pt) != b.is_valid_point(pt))
            return false;
        if (a.is_valid_point(pt) && glm::length(a.get_point(pt, doc) - b.get_point(pt, doc)) > 1e-6)
            return false;
    }
    return true;
}
} // namespace

bool give_own_workplane(Document &doc, const UUID &body)
{
    const auto body_groups = doc.get_groups_by_body();
    auto it = std::ranges::find_if(body_groups, [&body](const auto &bg) { return bg.get_group().m_uuid == body; });
    if (it == body_groups.end())
        return false;
    const auto old_uu = doc.get_group(body).m_active_wrkpl;
    auto old = doc.get_entity_ptr<EntityWorkplane>(old_uu);
    if (!old)
        return false;
    if (old->m_group == body)
        return true; // already has one
    std::set<UUID> group_set;
    for (auto gr : it->groups)
        group_set.insert(gr->m_uuid);

    auto &wp = doc.add_entity<EntityWorkplane>(UUID::random());
    wp.m_group = body;
    wp.m_origin = old->m_origin;
    wp.m_normal = old->m_normal;
    wp.m_size = {10, 10};
    wp.m_origin_fixed = true;
    wp.m_visible = false; // an inner detail: it would stay behind when the shape moves sideways
    auto &lock = doc.add_constraint<ConstraintLockRotation>(UUID::random());
    lock.m_group = body;
    lock.m_entity = wp.m_uuid;
    const std::map<std::string, std::string> smap{
            {static_cast<std::string>(old_uu), static_cast<std::string>(wp.m_uuid)}};

    for (const auto &uu : group_set) {
        auto &gr = doc.get_group(uu);
        if (gr.m_active_wrkpl == old_uu)
            gr.m_active_wrkpl = wp.m_uuid;
        if (auto sweep = dynamic_cast<GroupSweep *>(&gr); sweep && sweep->m_wrkpl == old_uu)
            sweep->m_wrkpl = wp.m_uuid;
    }
    std::vector<UUID> entities, constraints;
    for (const auto &[uu, en] : doc.m_entities) {
        if (en->m_kind == ItemKind::USER && group_set.contains(en->m_group) && uu != wp.m_uuid)
            entities.push_back(uu);
    }
    for (const auto &[uu, co] : doc.m_constraints) {
        if (group_set.contains(co->m_group) && uu != lock.m_uuid)
            constraints.push_back(uu);
    }
    // everything that points at the old plane (sketch entities, dimensions measured from its
    // origin) is pointed at the new one
    for (const auto &uu : entities) {
        auto j = doc.get_entity(uu).serialize();
        remap(j, smap);
        doc.m_entities.at(uu) = Entity::new_from_json(uu, j, {});
    }
    for (const auto &uu : constraints) {
        auto j = doc.m_constraints.at(uu)->serialize();
        remap(j, smap);
        doc.m_constraints.at(uu) = Constraint::new_from_json(uu, j);
    }
    for (const auto &g : it->groups)
        doc.set_group_solve_pending(g->m_uuid);
    doc.set_group_generate_pending(body);
    return true;
}

Group &duplicate_body(Document &doc, const UUID &body, const glm::dvec2 &offset)
{
    const auto body_groups = doc.get_groups_by_body();
    auto it = std::ranges::find_if(body_groups, [&body](const auto &bg) { return bg.get_group().m_uuid == body; });
    if (it == body_groups.end())
        throw std::runtime_error("no such body");
    std::vector<UUID> groups;
    for (auto gr : it->groups)
        groups.push_back(gr->m_uuid);
    const std::set<UUID> group_set(groups.begin(), groups.end());

    std::map<UUID, UUID> map;
    for (const auto &uu : groups)
        map.emplace(uu, UUID::random());
    std::vector<UUID> entities;
    for (const auto &[uu, en] : doc.m_entities) {
        if (en->m_kind == ItemKind::USER && group_set.contains(en->m_group)) {
            entities.push_back(uu);
            map.emplace(uu, UUID::random());
        }
    }
    std::vector<UUID> constraints;
    for (const auto &[uu, co] : doc.m_constraints) {
        if (group_set.contains(co->m_group)) {
            constraints.push_back(uu);
            map.emplace(uu, UUID::random());
        }
    }
    std::map<std::string, std::string> smap;
    for (const auto &[a, b] : map)
        smap.emplace(static_cast<std::string>(a), static_cast<std::string>(b));

    // groups go after everything, in order
    UUID after = doc.get_groups_sorted().back()->m_uuid;
    for (const auto &uu : groups) {
        auto j = doc.get_group(uu).serialize(doc);
        remap(j, smap);
        auto gr = Group::new_from_json(map.at(uu), j);
        doc.insert_group(std::move(gr), after);
        after = map.at(uu);
    }
    for (const auto &uu : entities) {
        auto j = doc.get_entity(uu).serialize();
        remap(j, smap);
        doc.m_entities.emplace(map.at(uu), Entity::new_from_json(map.at(uu), j, {}));
    }

    // Constraints on entities the groups generate (e.g. an extrusion's height) can only be
    // pointed at their copies once those exist: generate first, then match by geometry.
    std::vector<UUID> later;
    for (const auto &uu : constraints) {
        auto &co = *doc.m_constraints.at(uu);
        bool refs_generated = false;
        for (const auto &enp : co.get_referenced_entities_and_points()) {
            auto en = doc.get_entity_ptr(enp.entity);
            if (en && en->m_kind != ItemKind::USER && group_set.contains(en->m_group))
                refs_generated = true;
        }
        if (refs_generated) {
            later.push_back(uu);
            continue;
        }
        auto j = co.serialize();
        remap(j, smap);
        doc.m_constraints.emplace(map.at(uu), Constraint::new_from_json(map.at(uu), j));
    }
    doc.set_group_generate_pending(map.at(groups.front()));
    doc.update_pending();

    if (later.size()) {
        auto gen_map = smap;
        for (const auto &uu : later) {
            for (const auto &enp : doc.m_constraints.at(uu)->get_referenced_entities_and_points()) {
                auto &old = doc.get_entity(enp.entity);
                if (old.m_kind == ItemKind::USER || !group_set.contains(old.m_group))
                    continue;
                const auto new_group = map.at(old.m_group);
                for (const auto &[nuu, en] : doc.m_entities) {
                    if (en->m_group == new_group && same_geometry(doc, old, *en)) {
                        gen_map[static_cast<std::string>(enp.entity)] = static_cast<std::string>(nuu);
                        break;
                    }
                }
            }
        }
        for (const auto &uu : later) {
            auto j = doc.m_constraints.at(uu)->serialize();
            remap(j, gen_map);
            doc.m_constraints.emplace(map.at(uu), Constraint::new_from_json(map.at(uu), j));
        }
    }

    // move it: the position is horizontal/vertical distances from a workplane origin
    if (offset != glm::dvec2(0, 0)) {
        for (const auto &uu : constraints) {
            auto hv = dynamic_cast<ConstraintPointDistanceHV *>(doc.m_constraints.at(map.at(uu)).get());
            if (!hv)
                continue;
            auto en = doc.get_entity_ptr(hv->m_entity1.entity);
            if (!en || en->get_type() != Entity::Type::WORKPLANE)
                continue;
            hv->m_distance += hv->get_type() == Constraint::Type::POINT_DISTANCE_HORIZONTAL ? offset.x : offset.y;
        }
    }

    auto &first = doc.get_group(map.at(groups.front()));
    if (first.m_body)
        first.m_body->m_name += " copy";
    for (const auto &uu : groups)
        doc.set_group_solve_pending(map.at(uu));
    doc.set_group_generate_pending(first.m_uuid);
    return first;
}

} // namespace dune3d

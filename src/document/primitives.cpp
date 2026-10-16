#include "primitives.hpp"
#include "body_solids.hpp"
#include "document/document.hpp"
#include "document/group/all_groups.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_circle2d.hpp"
#include "document/entity/entity_point2d.hpp"
#include "document/entity/entity_bezier2d.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "document/constraint/constraint_hv.hpp"
#include "document/constraint/constraint_midpoint.hpp"
#include "document/constraint/constraint_point_on_line.hpp"
#include "document/constraint/constraint_point_distance.hpp"
#include "document/constraint/constraint_point_distance_hv.hpp"
#include "document/constraint/constraint_diameter_radius.hpp"
#include "document/constraint/constraint_lock_rotation.hpp"
#include "document/constraint/constraint_point_on_circle.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>

namespace dune3d {

/*
 * Ready-made solids (box, cylinder, sphere). Each one is built from the
 * regular parametric building blocks: a fully constrained sketch plus an
 * extrude or revolve group, all in a new body. Size and position on the
 * workplane are ordinary dimensions (the position as horizontal/vertical
 * distances from the workplane origin), so they can be edited afterwards
 * like in any other sketch. Solids sit on the workplane, like on a table.
 */

namespace {
class PrimitiveBuilder {
public:
    PrimitiveBuilder(Document &doc, const UUID &group, const UUID &wrkpl) : m_doc(doc), m_group(group), m_wrkpl(wrkpl)
    {
    }

    template <typename T> T &entity(const UUID &wrkpl)
    {
        auto &en = m_doc.add_entity<T>(UUID::random());
        en.m_group = m_group;
        en.m_wrkpl = wrkpl;
        return en;
    }

    template <typename T> T &entity()
    {
        return entity<T>(m_wrkpl);
    }

    template <typename T> T &constraint(const UUID &group)
    {
        auto &co = m_doc.add_constraint<T>(UUID::random());
        co.m_group = group;
        return co;
    }

    template <typename T> T &constraint()
    {
        return constraint<T>(m_group);
    }

    void coincident(const EntityAndPoint &a, const EntityAndPoint &b, const UUID &wrkpl)
    {
        auto &co = constraint<ConstraintPointsCoincident>();
        co.m_entity1 = a;
        co.m_entity2 = b;
        co.m_wrkpl = wrkpl;
    }

    void coincident(const EntityAndPoint &a, const EntityAndPoint &b)
    {
        coincident(a, b, m_wrkpl);
    }

    template <typename T> void hv(const EntityAndPoint &a, const EntityAndPoint &b, const UUID &wrkpl)
    {
        auto &co = constraint<T>();
        co.m_entity1 = a;
        co.m_entity2 = b;
        co.m_wrkpl = wrkpl;
    }

    // place a point at the given position: signed horizontal/vertical distances from the origin
    void position(const EntityAndPoint &pt, const glm::dvec2 &pos)
    {
        {
            auto &co = constraint<ConstraintPointDistanceHorizontal>();
            co.m_entity1 = origin();
            co.m_entity2 = pt;
            co.m_wrkpl = m_wrkpl;
            co.m_distance = pos.x;
            co.m_offset = {pos.x / 2, pos.y - 8, 0};
        }
        {
            auto &co = constraint<ConstraintPointDistanceVertical>();
            co.m_entity1 = origin();
            co.m_entity2 = pt;
            co.m_wrkpl = m_wrkpl;
            co.m_distance = pos.y;
            co.m_offset = {pos.x - 8, pos.y / 2, 0};
        }
    }

    EntityAndPoint origin() const
    {
        return {m_wrkpl, 1};
    }

    Document &m_doc;
    const UUID m_group;
    const UUID m_wrkpl;
};

// An upright plane standing on the workplane at pos, for profiles that get
// revolved or extruded sideways. Its "foot" is placed like the centre of the
// other shapes and its rotation is locked. x runs along the workplane's x,
// y points up.
UUID add_upright_plane(PrimitiveBuilder &b, const EntityWorkplane &wrkpl, const glm::dvec2 &pos, double size)
{
    auto &foot = b.entity<EntityPoint2D>();
    foot.m_construction = true;
    foot.m_p = pos;
    b.position({foot.m_uuid, 0}, pos);

    const auto normal = wrkpl.get_normal_vector();
    const auto x = wrkpl.transform_relative({1, 0});
    auto &upright = b.m_doc.add_entity<EntityWorkplane>(UUID::random());
    upright.m_group = b.m_group;
    upright.m_origin = wrkpl.transform(pos);
    upright.m_normal = glm::quat_cast(glm::dmat3(x, normal, glm::cross(x, normal)));
    upright.m_size = {size, size}; // a helper only, small enough to stay hidden
    b.coincident({upright.m_uuid, 1}, {foot.m_uuid, 0}, UUID());
    b.constraint<ConstraintLockRotation>().m_entity = upright.m_uuid;
    return upright.m_uuid;
}

// closed polygon of lines, consecutive ends made coincident
std::vector<EntityLine2D *> add_polygon(PrimitiveBuilder &b, const UUID &wrkpl, const std::vector<glm::dvec2> &pts)
{
    std::vector<EntityLine2D *> lines;
    for (size_t i = 0; i < pts.size(); i++) {
        auto &li = b.entity<EntityLine2D>(wrkpl);
        li.m_p1 = pts.at(i);
        li.m_p2 = pts.at((i + 1) % pts.size());
        lines.push_back(&li);
    }
    for (size_t i = 0; i < lines.size(); i++)
        b.coincident({lines.at(i)->m_uuid, 2}, {lines.at((i + 1) % lines.size())->m_uuid, 1}, wrkpl);
    return lines;
}

template <typename T> void add_hv_distance(PrimitiveBuilder &b, const EntityAndPoint &p1, const EntityAndPoint &p2,
                                           const UUID &wrkpl, double d)
{
    auto &co = b.constraint<T>();
    co.m_entity1 = p1;
    co.m_entity2 = p2;
    co.m_wrkpl = wrkpl;
    co.m_distance = d;
}

GroupRevolve &add_full_revolve(Document &doc, const UUID &sketch, const UUID &wrkpl, const UUID &axis)
{
    auto &rev = doc.insert_group<GroupRevolve>(UUID::random(), sketch);
    rev.m_wrkpl = wrkpl;
    rev.m_source_group = sketch;
    rev.m_origin = {axis, 1};
    rev.m_normal = axis;
    rev.m_angle = 360;
    return rev;
}

} // namespace

GroupExtrude &add_extrusion(Document &doc, const UUID &sketch, const UUID &wrkpl, double height, bool symmetric)
{
    auto &ex = doc.insert_group<GroupExtrude>(UUID::random(), sketch);
    ex.m_wrkpl = wrkpl;
    ex.m_source_group = sketch;
    if (symmetric)
        ex.m_mode = GroupExtrude::Mode::OFFSET_SYMMETRIC;
    ex.m_dvec = doc.get_entity<EntityWorkplane>(wrkpl).get_normal_vector() * height;
    auto &dh = doc.add_constraint<ConstraintPointDistance>(UUID::random());
    dh.m_group = ex.m_uuid;
    dh.m_entity1 = {ex.get_leader_line_uuid(GroupExtrude::Side::TOP), 1};
    dh.m_entity2 = {ex.get_leader_line_uuid(GroupExtrude::Side::TOP), 2};
    dh.m_distance = height;
    return ex;
}

void add_position(Document &doc, const UUID &sketch, const UUID &wrkpl, const EntityAndPoint &pt,
                  const glm::dvec2 &pos)
{
    PrimitiveBuilder{doc, sketch, wrkpl}.position(pt, pos);
}

namespace {
// the new body's sketch, standing on the active workplane or one of its own
struct NewBody {
    GroupSketch &sketch;
    UUID wrkpl;
};

NewBody begin_body(Document &doc, const UUID &after, const std::optional<Placement> &placement)
{
    auto &current_group = doc.get_group(after);
    auto &sketch = doc.insert_group<GroupSketch>(UUID::random(), current_group.m_uuid);
    sketch.m_body.emplace();
    UUID wrkpl_uu = current_group.m_active_wrkpl;
    if (!wrkpl_uu)
        wrkpl_uu = doc.get_reference_group().get_workplane_xy_uuid();
    if (placement) {
        auto &wp = doc.add_entity<EntityWorkplane>(UUID::random());
        wp.m_group = sketch.m_uuid;
        wp.m_origin = placement->origin;
        wp.m_normal = glm::normalize(placement->rotation);
        wp.m_size = {10, 10};
        auto &lock = doc.add_constraint<ConstraintLockRotation>(UUID::random());
        lock.m_group = sketch.m_uuid;
        lock.m_entity = wp.m_uuid;
        wrkpl_uu = wp.m_uuid;
    }
    sketch.m_active_wrkpl = wrkpl_uu;
    return {sketch, wrkpl_uu};
}

void end_body(Document &doc, GroupSketch &sketch, Group &solid_group, const std::string &body_name)
{
    sketch.m_name = doc.find_next_group_name(Group::Type::SKETCH);
    solid_group.m_name = body_name;
    {
        // number bodies of the same kind: "Box", "Box 2", ...
        unsigned int n = 0;
        for (const auto &[uu, gr] : doc.get_groups()) {
            if (gr->m_body && gr->m_body->m_name.starts_with(body_name))
                n++;
        }
        sketch.m_body->m_name = n ? (body_name + " " + std::to_string(n + 1)) : body_name;
    }
    doc.set_group_generate_pending(sketch.m_uuid);
    doc.set_group_generate_pending(solid_group.m_uuid);
}

// a smooth curve through the points (Catmull-Rom), as cubic Bezier pieces
// between consecutive points; closed: back to the first one too
std::vector<std::array<glm::dvec2, 4>> smooth_curve(const std::vector<glm::dvec2> &pts, bool closed)
{
    std::vector<std::array<glm::dvec2, 4>> r;
    const auto n = pts.size();
    auto at = [&](long i) {
        if (closed)
            return pts.at((i % (long)n + n) % n);
        return pts.at(std::clamp<long>(i, 0, n - 1));
    };
    const size_t segs = closed ? n : n - 1;
    for (size_t i = 0; i < segs; i++) {
        const auto p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
        r.push_back({p1, p1 + (p2 - p0) / 6., p2 - (p3 - p1) / 6., p2});
    }
    return r;
}

// the outline's pieces in order: straight lines, or Bezier curves where smooth;
// returns the entities with their start and end point numbers
struct Piece {
    UUID entity;
    unsigned int start, end;
};
Piece add_line(PrimitiveBuilder &b, const UUID &wrkpl, glm::dvec2 a, glm::dvec2 c)
{
    auto &li = b.entity<EntityLine2D>(wrkpl);
    li.m_p1 = a;
    li.m_p2 = c;
    return {li.m_uuid, 1, 2};
}
Piece add_bezier(PrimitiveBuilder &b, const UUID &wrkpl, const std::array<glm::dvec2, 4> &bz)
{
    auto &be = b.entity<EntityBezier2D>(wrkpl);
    be.m_p1 = bz.at(0);
    be.m_c1 = bz.at(1);
    be.m_c2 = bz.at(2);
    be.m_p2 = bz.at(3);
    return {be.m_uuid, 1, 2};
}
void connect(PrimitiveBuilder &b, const UUID &wrkpl, const std::vector<Piece> &pieces, bool closed)
{
    for (size_t i = 0; i + 1 < pieces.size() + (closed ? 1 : 0); i++) {
        const auto &a = pieces.at(i);
        const auto &c = pieces.at((i + 1) % pieces.size());
        b.coincident({a.entity, a.end}, {c.entity, c.start}, wrkpl);
    }
}
} // namespace

Group &add_revolved_profile(Document &doc, const UUID &after, std::vector<glm::dvec2> profile, bool smooth,
                            const glm::dvec2 &pos, const std::optional<Placement> &placement)
{
    for (auto &p : profile)
        p.x = std::max(p.x, 0.);
    // start and end on the axis
    if (profile.empty() || profile.front().x > 0)
        profile.insert(profile.begin(), {0, profile.empty() ? 0 : profile.front().y});
    if (profile.back().x > 0)
        profile.push_back({0, profile.back().y});
    if (profile.size() < 3)
        throw std::runtime_error("a profile needs at least one point off the axis");

    auto [sketch, wrkpl_uu] = begin_body(doc, after, placement);
    PrimitiveBuilder b{doc, sketch.m_uuid, wrkpl_uu};
    const auto &wrkpl = doc.get_entity<EntityWorkplane>(wrkpl_uu);
    const auto up = add_upright_plane(b, wrkpl, placement ? glm::dvec2(0, 0) : pos, 2);

    std::vector<Piece> pieces;
    const auto n = profile.size();
    if (smooth && n > 3) {
        // straight from the axis, a curve through the points in between, straight back
        pieces.push_back(add_line(b, up, profile.at(0), profile.at(1)));
        std::vector<glm::dvec2> mid(profile.begin() + 1, profile.end() - 1);
        for (const auto &bz : smooth_curve(mid, false))
            pieces.push_back(add_bezier(b, up, bz));
        pieces.push_back(add_line(b, up, profile.at(n - 2), profile.at(n - 1)));
    }
    else {
        for (size_t i = 0; i + 1 < n; i++)
            pieces.push_back(add_line(b, up, profile.at(i), profile.at(i + 1)));
    }
    auto axis = add_line(b, up, profile.back(), profile.front());
    pieces.push_back(axis);
    connect(b, up, pieces, true);
    // the axis stands upright through the plane's foot
    b.hv<ConstraintVertical>({axis.entity, 1}, {axis.entity, 2}, up);
    {
        auto &on = b.constraint<ConstraintPointOnLine>();
        on.m_line = axis.entity;
        on.m_point = {up, 1};
        on.m_wrkpl = up;
    }
    auto &rev = add_full_revolve(doc, sketch.m_uuid, up, axis.entity);
    end_body(doc, sketch, rev, "Revolved shape");
    return rev;
}

Group &add_extruded_outline(Document &doc, const UUID &after, const std::vector<glm::dvec2> &outline, double height,
                            bool smooth, const glm::dvec2 &pos, const std::optional<Placement> &placement)
{
    if (outline.size() < 3)
        throw std::runtime_error("an outline needs at least three points");
    auto [sketch, wrkpl_uu] = begin_body(doc, after, placement);
    PrimitiveBuilder b{doc, sketch.m_uuid, wrkpl_uu};
    const auto offset = placement ? glm::dvec2(0, 0) : pos;
    std::vector<glm::dvec2> pts;
    for (const auto &p : outline)
        pts.push_back(p + offset);
    std::vector<Piece> pieces;
    if (smooth) {
        for (const auto &bz : smooth_curve(pts, true))
            pieces.push_back(add_bezier(b, wrkpl_uu, bz));
    }
    else {
        for (size_t i = 0; i < pts.size(); i++)
            pieces.push_back(add_line(b, wrkpl_uu, pts.at(i), pts.at((i + 1) % pts.size())));
    }
    connect(b, wrkpl_uu, pieces, true);
    auto &ex = add_extrusion(doc, sketch.m_uuid, wrkpl_uu, height);
    end_body(doc, sketch, ex, "Extruded shape");
    return ex;
}

Group &add_primitive(Document &doc, const UUID &after, Primitive primitive, const glm::dvec2 &pos_in,
                     const PrimitiveParams &params, const std::optional<Placement> &placement)
{
    auto [sketch, wrkpl_uu] = begin_body(doc, after, placement);
    const auto &wrkpl = doc.get_entity<EntityWorkplane>(wrkpl_uu);
    const auto sk = sketch.m_uuid;
    // on a workplane of its own, it's centred on that one's origin
    const auto pos = placement ? glm::dvec2(0, 0) : pos_in;

    PrimitiveBuilder b{doc, sk, wrkpl_uu};

    Group *solid_group = nullptr;
    std::string body_name;

    switch (primitive) {
    case Primitive::BOX: {
        body_name = "Box";
        const double w = params.width, d = params.depth, h = params.height;
        const glm::dvec2 pts[] = {pos + glm::dvec2(-w / 2, -d / 2), pos + glm::dvec2(w / 2, -d / 2),
                                  pos + glm::dvec2(w / 2, d / 2), pos + glm::dvec2(-w / 2, d / 2)};
        EntityLine2D *lines[4];
        for (int i = 0; i < 4; i++) {
            auto &li = b.entity<EntityLine2D>();
            li.m_p1 = pts[i];
            li.m_p2 = pts[(i + 1) % 4];
            lines[i] = &li;
        }
        for (int i = 0; i < 4; i++) {
            b.coincident({lines[i]->m_uuid, 2}, {lines[(i + 1) % 4]->m_uuid, 1});
            if (i % 2 == 0)
                b.hv<ConstraintHorizontal>({lines[i]->m_uuid, 1}, {lines[i]->m_uuid, 2}, wrkpl_uu);
            else
                b.hv<ConstraintVertical>({lines[i]->m_uuid, 1}, {lines[i]->m_uuid, 2}, wrkpl_uu);
        }
        // the centre is the midpoint of a construction diagonal, placed at pos
        auto &diag = b.entity<EntityLine2D>();
        diag.m_construction = true;
        diag.m_p1 = pts[0];
        diag.m_p2 = pts[2];
        b.coincident({diag.m_uuid, 1}, {lines[0]->m_uuid, 1});
        b.coincident({diag.m_uuid, 2}, {lines[2]->m_uuid, 1});
        auto &center = b.entity<EntityPoint2D>();
        center.m_construction = true;
        center.m_p = pos;
        {
            auto &mid = b.constraint<ConstraintMidpoint>();
            mid.m_line = diag.m_uuid;
            mid.m_point = {center.m_uuid, 0};
            mid.m_wrkpl = wrkpl_uu;
        }
        b.position({center.m_uuid, 0}, pos);
        {
            auto &dw = b.constraint<ConstraintPointDistanceHorizontal>();
            dw.m_entity1 = {lines[0]->m_uuid, 1};
            dw.m_entity2 = {lines[0]->m_uuid, 2};
            dw.m_wrkpl = wrkpl_uu;
            dw.m_distance = w;
            dw.m_offset = {pos.x, pos.y - d / 2 - 5, 0};
        }
        {
            auto &dd = b.constraint<ConstraintPointDistanceVertical>();
            dd.m_entity1 = {lines[1]->m_uuid, 1};
            dd.m_entity2 = {lines[1]->m_uuid, 2};
            dd.m_wrkpl = wrkpl_uu;
            dd.m_distance = d;
            dd.m_offset = {pos.x + w / 2 + 5, pos.y, 0};
        }
        solid_group = &add_extrusion(doc, sk, wrkpl_uu, h);
        break;
    }

    case Primitive::CYLINDER: {
        body_name = "Cylinder";
        const double r = params.diameter / 2, h = params.height;
        auto &circle = b.entity<EntityCircle2D>();
        circle.m_center = pos;
        circle.m_radius = r;
        b.position({circle.m_uuid, 1}, pos);
        auto &dia = b.constraint<ConstraintDiameter>();
        dia.m_entity = circle.m_uuid;
        dia.m_distance = 2 * r;
        solid_group = &add_extrusion(doc, sk, wrkpl_uu, h);
        break;
    }

    case Primitive::SPHERE: {
        body_name = "Sphere";
        const double r = params.diameter / 2;
        // the profile lives on an upright plane, so the sphere rests on the workplane
        const auto up = add_upright_plane(b, wrkpl, pos, r / 4);

        // half circle on the +x side, closed by the vertical axis it revolves around
        auto &arc = b.entity<EntityArc2D>(up);
        arc.m_center = {0, r};
        arc.m_from = {0, 0};
        arc.m_to = {0, 2 * r};
        auto &axis = b.entity<EntityLine2D>(up);
        axis.m_p1 = {0, 2 * r};
        axis.m_p2 = {0, 0};
        b.coincident({axis.m_uuid, 2}, {up, 1}, up);
        b.coincident({arc.m_uuid, 2}, {axis.m_uuid, 1}, up);
        b.coincident({arc.m_uuid, 1}, {axis.m_uuid, 2}, up);
        b.hv<ConstraintVertical>({axis.m_uuid, 1}, {axis.m_uuid, 2}, up);
        {
            // keep the axis a diameter rather than an arbitrary chord
            auto &on = b.constraint<ConstraintPointOnLine>();
            on.m_line = axis.m_uuid;
            on.m_point = {arc.m_uuid, 3};
            on.m_wrkpl = up;
        }
        auto &rad = b.constraint<ConstraintRadius>();
        rad.m_entity = arc.m_uuid;
        rad.m_distance = r;
        solid_group = &add_full_revolve(doc, sk, up, axis.m_uuid);
        break;
    }

    case Primitive::CONE: {
        body_name = "Cone";
        const double r = params.diameter / 2, h = params.height;
        // right triangle: base, slant, axis; revolved around the axis
        const auto up = add_upright_plane(b, wrkpl, pos, r / 4);
        auto lines = add_polygon(b, up, {{0, 0}, {r, 0}, {0, h}});
        auto base = lines.at(0), axis = lines.at(2);
        b.coincident({base->m_uuid, 1}, {up, 1}, up);
        b.hv<ConstraintHorizontal>({base->m_uuid, 1}, {base->m_uuid, 2}, up);
        b.hv<ConstraintVertical>({axis->m_uuid, 1}, {axis->m_uuid, 2}, up);
        add_hv_distance<ConstraintPointDistanceHorizontal>(b, {base->m_uuid, 1}, {base->m_uuid, 2}, up, r);
        add_hv_distance<ConstraintPointDistanceVertical>(b, {axis->m_uuid, 2}, {axis->m_uuid, 1}, up, h);
        solid_group = &add_full_revolve(doc, sk, up, axis->m_uuid);
        break;
    }

    case Primitive::TUBE: {
        body_name = "Tube";
        const double r_out = params.diameter / 2, r_in = std::min(params.inner_diameter, params.diameter * .95) / 2,
                     h = params.height;
        auto &outer = b.entity<EntityCircle2D>();
        outer.m_center = pos;
        outer.m_radius = r_out;
        auto &inner = b.entity<EntityCircle2D>();
        inner.m_center = pos;
        inner.m_radius = r_in;
        b.position({outer.m_uuid, 1}, pos);
        b.coincident({inner.m_uuid, 1}, {outer.m_uuid, 1});
        for (auto [circle, r] : {std::pair{&outer, r_out}, std::pair{&inner, r_in}}) {
            auto &dia = b.constraint<ConstraintDiameter>();
            dia.m_entity = circle->m_uuid;
            dia.m_distance = 2 * r;
        }
        solid_group = &add_extrusion(doc, sk, wrkpl_uu, h);
        break;
    }

    case Primitive::TORUS: {
        body_name = "Torus";
        const double ring = params.ring_diameter / 2, tube = std::min(params.tube_diameter, params.ring_diameter * .95) / 2;
        // circle standing on a horizontal construction line from the axis, revolved
        // around the (construction) vertical axis; it touches the workplane
        const auto up = add_upright_plane(b, wrkpl, pos, tube / 2);
        auto &axis = b.entity<EntityLine2D>(up);
        axis.m_construction = true;
        axis.m_p1 = {0, 0};
        axis.m_p2 = {0, tube};
        b.coincident({axis.m_uuid, 1}, {up, 1}, up);
        b.hv<ConstraintVertical>({axis.m_uuid, 1}, {axis.m_uuid, 2}, up);
        auto &spoke = b.entity<EntityLine2D>(up);
        spoke.m_construction = true;
        spoke.m_p1 = {0, 0};
        spoke.m_p2 = {ring, 0};
        b.coincident({spoke.m_uuid, 1}, {up, 1}, up);
        b.hv<ConstraintHorizontal>({spoke.m_uuid, 1}, {spoke.m_uuid, 2}, up);
        add_hv_distance<ConstraintPointDistanceHorizontal>(b, {spoke.m_uuid, 1}, {spoke.m_uuid, 2}, up, ring);
        auto &circle = b.entity<EntityCircle2D>(up);
        circle.m_center = {ring, tube};
        circle.m_radius = tube;
        {
            auto &on = b.constraint<ConstraintPointOnCircle>();
            on.m_point = {spoke.m_uuid, 2};
            on.m_circle = circle.m_uuid;
        }
        b.hv<ConstraintVertical>({spoke.m_uuid, 2}, {circle.m_uuid, 1}, up);
        auto &dia = b.constraint<ConstraintDiameter>();
        dia.m_entity = circle.m_uuid;
        dia.m_distance = 2 * tube;
        // the axis reaches up to the height of the circle's centre
        b.hv<ConstraintHorizontal>({axis.m_uuid, 2}, {circle.m_uuid, 1}, up);
        solid_group = &add_full_revolve(doc, sk, up, axis.m_uuid);
        break;
    }

    case Primitive::WEDGE: {
        body_name = "Wedge";
        const double w = params.width, h = params.height, d = params.depth;
        // right triangle profile on an upright plane, extruded sideways
        const auto up = add_upright_plane(b, wrkpl, pos, 2);
        auto lines = add_polygon(b, up, {{-w / 2, 0}, {w / 2, 0}, {-w / 2, h}});
        auto base = lines.at(0), back = lines.at(2);
        b.hv<ConstraintHorizontal>({base->m_uuid, 1}, {base->m_uuid, 2}, up);
        b.hv<ConstraintVertical>({back->m_uuid, 1}, {back->m_uuid, 2}, up);
        {
            // centred on the foot along x
            auto &mid = b.constraint<ConstraintMidpoint>();
            mid.m_line = base->m_uuid;
            mid.m_point = {up, 1};
            mid.m_wrkpl = up;
        }
        add_hv_distance<ConstraintPointDistanceHorizontal>(b, {base->m_uuid, 1}, {base->m_uuid, 2}, up, w);
        add_hv_distance<ConstraintPointDistanceVertical>(b, {back->m_uuid, 2}, {back->m_uuid, 1}, up, h);
        // symmetric: centred on the foot along y too
        solid_group = &add_extrusion(doc, sk, up, d / 2, true);
        break;
    }
    }

    end_body(doc, sketch, *solid_group, body_name);
    return *solid_group;
}

glm::dvec2 find_free_spot(const Document &doc, const UUID &upto, double footprint)
{
    Bnd_Box box;
    for (const auto &bs : get_body_solids(doc, upto))
        BRepBndLib::Add(bs.solid->m_shape_acc, box, true); // by the triangles: meshes have no surfaces
    if (box.IsVoid())
        return {0, 0};
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    // next to what's there, in a row along +x, rounded to whole millimetres
    return {std::ceil(x1 + footprint / 2 + 10), 0};
}

} // namespace dune3d

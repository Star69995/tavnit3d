#include "primitives.hpp"
#include "body_solids.hpp"
#include "document/document.hpp"
#include "document/group/all_groups.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_circle2d.hpp"
#include "document/entity/entity_point2d.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "document/constraint/constraint_hv.hpp"
#include "document/constraint/constraint_midpoint.hpp"
#include "document/constraint/constraint_point_on_line.hpp"
#include "document/constraint/constraint_point_distance.hpp"
#include "document/constraint/constraint_point_distance_hv.hpp"
#include "document/constraint/constraint_diameter_radius.hpp"
#include "document/constraint/constraint_lock_rotation.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <glm/gtc/quaternion.hpp>

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

void constrain_extrusion_height(PrimitiveBuilder &b, GroupExtrude &ex, double h)
{
    auto &dh = b.constraint<ConstraintPointDistance>(ex.m_uuid);
    dh.m_entity1 = {ex.get_leader_line_uuid(GroupExtrude::Side::TOP), 1};
    dh.m_entity2 = {ex.get_leader_line_uuid(GroupExtrude::Side::TOP), 2};
    dh.m_distance = h;
}
} // namespace

Group &add_primitive(Document &doc, const UUID &after, Primitive primitive, const glm::dvec2 &pos)
{
    auto &current_group = doc.get_group(after);

    UUID wrkpl_uu = current_group.m_active_wrkpl;
    if (!wrkpl_uu)
        wrkpl_uu = doc.get_reference_group().get_workplane_xy_uuid();
    const auto &wrkpl = doc.get_entity<EntityWorkplane>(wrkpl_uu);
    const auto normal = wrkpl.get_normal_vector();

    auto &sketch = doc.insert_group<GroupSketch>(UUID::random(), current_group.m_uuid);
    sketch.m_active_wrkpl = wrkpl_uu;
    sketch.m_body.emplace();
    const auto sk = sketch.m_uuid;

    PrimitiveBuilder b{doc, sk, wrkpl_uu};

    Group *solid_group = nullptr;
    std::string body_name;

    switch (primitive) {
    case Primitive::BOX: {
        body_name = "Box";
        const double w = 20, d = 20, h = 20;
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
        auto &ex = doc.insert_group<GroupExtrude>(UUID::random(), sk);
        ex.m_wrkpl = wrkpl_uu;
        ex.m_source_group = sk;
        ex.m_dvec = normal * h;
        solid_group = &ex;
        constrain_extrusion_height(b, ex, h);
        break;
    }

    case Primitive::CYLINDER: {
        body_name = "Cylinder";
        const double r = 10, h = 20;
        auto &circle = b.entity<EntityCircle2D>();
        circle.m_center = pos;
        circle.m_radius = r;
        b.position({circle.m_uuid, 1}, pos);
        auto &dia = b.constraint<ConstraintDiameter>();
        dia.m_entity = circle.m_uuid;
        dia.m_distance = 2 * r;
        auto &ex = doc.insert_group<GroupExtrude>(UUID::random(), sk);
        ex.m_wrkpl = wrkpl_uu;
        ex.m_source_group = sk;
        ex.m_dvec = normal * h;
        solid_group = &ex;
        constrain_extrusion_height(b, ex, h);
        break;
    }

    case Primitive::SPHERE: {
        body_name = "Sphere";
        const double r = 10;
        // The profile lives on an upright plane standing on the workplane at pos, so
        // the sphere rests on the workplane: its "foot" point is placed like the
        // centre of the other shapes, the plane's rotation is locked.
        auto &foot = b.entity<EntityPoint2D>();
        foot.m_construction = true;
        foot.m_p = pos;
        b.position({foot.m_uuid, 0}, pos);

        const auto x = wrkpl.transform_relative({1, 0});
        auto &upright = doc.add_entity<EntityWorkplane>(UUID::random());
        upright.m_group = sk;
        upright.m_origin = wrkpl.transform(pos);
        upright.m_normal = glm::quat_cast(glm::dmat3(x, normal, glm::cross(x, normal)));
        upright.m_size = {r / 4, r / 4}; // a helper only; small enough to stay inside the sphere
        b.coincident({upright.m_uuid, 1}, {foot.m_uuid, 0}, UUID());
        b.constraint<ConstraintLockRotation>().m_entity = upright.m_uuid;
        const auto up = upright.m_uuid;

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
        auto &rev = doc.insert_group<GroupRevolve>(UUID::random(), sk);
        rev.m_wrkpl = up;
        rev.m_source_group = sk;
        rev.m_origin = {axis.m_uuid, 1};
        rev.m_normal = axis.m_uuid;
        rev.m_angle = 360;
        solid_group = &rev;
        break;
    }
    }

    sketch.m_name = doc.find_next_group_name(Group::Type::SKETCH);
    solid_group->m_name = body_name;
    {
        // number bodies of the same kind: "Box", "Box 2", ...
        unsigned int n = 0;
        for (const auto &[uu, gr] : doc.get_groups()) {
            if (gr->m_body && gr->m_body->m_name.starts_with(body_name))
                n++;
        }
        sketch.m_body->m_name = n ? (body_name + " " + std::to_string(n + 1)) : body_name;
    }

    doc.set_group_generate_pending(sk);
    doc.set_group_generate_pending(solid_group->m_uuid);
    return *solid_group;
}

glm::dvec2 find_free_spot(const Document &doc, const UUID &upto, double footprint)
{
    Bnd_Box box;
    for (const auto &bs : get_body_solids(doc, upto))
        BRepBndLib::AddOptimal(bs.solid->m_shape_acc, box, false, false);
    if (box.IsVoid())
        return {0, 0};
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    // next to what's there, in a row along +x, rounded to whole millimetres
    return {std::ceil(x1 + footprint / 2 + 10), 0};
}

} // namespace dune3d

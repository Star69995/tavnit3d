#include "primitives.hpp"
#include "document/document.hpp"
#include "document/group/all_groups.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_arc2d.hpp"
#include "document/entity/entity_circle2d.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "document/constraint/constraint_hv.hpp"
#include "document/constraint/constraint_midpoint.hpp"
#include "document/constraint/constraint_point_on_line.hpp"
#include "document/constraint/constraint_point_distance.hpp"
#include "document/constraint/constraint_point_distance_hv.hpp"
#include "document/constraint/constraint_diameter_radius.hpp"

namespace dune3d {

/*
 * Ready-made solids (box, cylinder, sphere). Each one is built from the
 * regular parametric building blocks: a fully constrained sketch centred
 * on the workplane origin plus an extrude or revolve group, all in a new
 * body. The dimensions are ordinary constraints, so they can be edited
 * afterwards like in any other sketch.
 */

namespace {
class PrimitiveBuilder {
public:
    PrimitiveBuilder(Document &doc, const UUID &wrkpl) : m_doc(doc), m_wrkpl(wrkpl)
    {
    }

    template <typename T> T &entity(const UUID &group)
    {
        auto &en = m_doc.add_entity<T>(UUID::random());
        en.m_group = group;
        en.m_wrkpl = m_wrkpl;
        return en;
    }

    template <typename T> T &constraint(const UUID &group)
    {
        auto &co = m_doc.add_constraint<T>(UUID::random());
        co.m_group = group;
        return co;
    }

    void coincident(const UUID &group, const EntityAndPoint &a, const EntityAndPoint &b)
    {
        auto &co = constraint<ConstraintPointsCoincident>(group);
        co.m_entity1 = a;
        co.m_entity2 = b;
        co.m_wrkpl = m_wrkpl;
    }

    EntityAndPoint origin() const
    {
        return {m_wrkpl, 1};
    }

    Document &m_doc;
    const UUID m_wrkpl;
};
} // namespace

Group &add_primitive(Document &doc, const UUID &after, Primitive primitive)
{
    auto &current_group = doc.get_group(after);

    UUID wrkpl = current_group.m_active_wrkpl;
    if (!wrkpl)
        wrkpl = doc.get_reference_group().get_workplane_xy_uuid();
    const auto normal = doc.get_entity<EntityWorkplane>(wrkpl).get_normal_vector();

    PrimitiveBuilder b{doc, wrkpl};

    auto &sketch = doc.insert_group<GroupSketch>(UUID::random(), current_group.m_uuid);
    sketch.m_active_wrkpl = wrkpl;
    sketch.m_body.emplace();
    const auto sk = sketch.m_uuid;

    Group *solid_group = nullptr;
    std::string body_name;

    switch (primitive) {
    case Primitive::BOX: {
        body_name = "Box";
        const double w = 20, d = 20, h = 20;
        const glm::dvec2 pts[] = {{-w / 2, -d / 2}, {w / 2, -d / 2}, {w / 2, d / 2}, {-w / 2, d / 2}};
        EntityLine2D *lines[4];
        for (int i = 0; i < 4; i++) {
            auto &li = b.entity<EntityLine2D>(sk);
            li.m_p1 = pts[i];
            li.m_p2 = pts[(i + 1) % 4];
            lines[i] = &li;
        }
        for (int i = 0; i < 4; i++) {
            b.coincident(sk, {lines[i]->m_uuid, 2}, {lines[(i + 1) % 4]->m_uuid, 1});
            ConstraintHV *hv = nullptr;
            if (i % 2 == 0)
                hv = &b.constraint<ConstraintHorizontal>(sk);
            else
                hv = &b.constraint<ConstraintVertical>(sk);
            hv->m_entity1 = {lines[i]->m_uuid, 1};
            hv->m_entity2 = {lines[i]->m_uuid, 2};
            hv->m_wrkpl = wrkpl;
        }
        // centre the rectangle on the origin via a construction diagonal
        auto &diag = b.entity<EntityLine2D>(sk);
        diag.m_construction = true;
        diag.m_p1 = pts[0];
        diag.m_p2 = pts[2];
        b.coincident(sk, {diag.m_uuid, 1}, {lines[0]->m_uuid, 1});
        b.coincident(sk, {diag.m_uuid, 2}, {lines[2]->m_uuid, 1});
        {
            auto &mid = b.constraint<ConstraintMidpoint>(sk);
            mid.m_line = diag.m_uuid;
            mid.m_point = b.origin();
            mid.m_wrkpl = wrkpl;
        }
        {
            auto &dw = b.constraint<ConstraintPointDistanceHorizontal>(sk);
            dw.m_entity1 = {lines[0]->m_uuid, 1};
            dw.m_entity2 = {lines[0]->m_uuid, 2};
            dw.m_wrkpl = wrkpl;
            dw.m_distance = w;
            dw.m_offset = {0, -d / 2 - 5, 0};
        }
        {
            auto &dd = b.constraint<ConstraintPointDistanceVertical>(sk);
            dd.m_entity1 = {lines[1]->m_uuid, 1};
            dd.m_entity2 = {lines[1]->m_uuid, 2};
            dd.m_wrkpl = wrkpl;
            dd.m_distance = d;
            dd.m_offset = {w / 2 + 5, 0, 0};
        }
        auto &ex = doc.insert_group<GroupExtrude>(UUID::random(), sk);
        ex.m_wrkpl = wrkpl;
        ex.m_source_group = sk;
        ex.m_dvec = normal * h;
        solid_group = &ex;
        auto &dh = b.constraint<ConstraintPointDistance>(ex.m_uuid);
        dh.m_entity1 = {ex.get_leader_line_uuid(GroupExtrude::Side::TOP), 1};
        dh.m_entity2 = {ex.get_leader_line_uuid(GroupExtrude::Side::TOP), 2};
        dh.m_distance = h;
        break;
    }

    case Primitive::CYLINDER: {
        body_name = "Cylinder";
        const double r = 10, h = 20;
        auto &circle = b.entity<EntityCircle2D>(sk);
        circle.m_center = {0, 0};
        circle.m_radius = r;
        b.coincident(sk, {circle.m_uuid, 1}, b.origin());
        auto &dia = b.constraint<ConstraintDiameter>(sk);
        dia.m_entity = circle.m_uuid;
        dia.m_distance = 2 * r;
        auto &ex = doc.insert_group<GroupExtrude>(UUID::random(), sk);
        ex.m_wrkpl = wrkpl;
        ex.m_source_group = sk;
        ex.m_dvec = normal * h;
        solid_group = &ex;
        auto &dh = b.constraint<ConstraintPointDistance>(ex.m_uuid);
        dh.m_entity1 = {ex.get_leader_line_uuid(GroupExtrude::Side::TOP), 1};
        dh.m_entity2 = {ex.get_leader_line_uuid(GroupExtrude::Side::TOP), 2};
        dh.m_distance = h;
        break;
    }

    case Primitive::SPHERE: {
        body_name = "Sphere";
        const double r = 10;
        // half circle on the +x side, closed by the axis line it revolves around
        auto &arc = b.entity<EntityArc2D>(sk);
        arc.m_center = {0, 0};
        arc.m_from = {0, -r};
        arc.m_to = {0, r};
        auto &axis = b.entity<EntityLine2D>(sk);
        axis.m_p1 = {0, r};
        axis.m_p2 = {0, -r};
        b.coincident(sk, {arc.m_uuid, 3}, b.origin());
        b.coincident(sk, {arc.m_uuid, 2}, {axis.m_uuid, 1});
        b.coincident(sk, {arc.m_uuid, 1}, {axis.m_uuid, 2});
        {
            auto &v = b.constraint<ConstraintVertical>(sk);
            v.m_entity1 = {axis.m_uuid, 1};
            v.m_entity2 = {axis.m_uuid, 2};
            v.m_wrkpl = wrkpl;
        }
        {
            // keep the axis a diameter rather than an arbitrary chord
            auto &on = b.constraint<ConstraintPointOnLine>(sk);
            on.m_line = axis.m_uuid;
            on.m_point = b.origin();
            on.m_wrkpl = wrkpl;
        }
        auto &rad = b.constraint<ConstraintRadius>(sk);
        rad.m_entity = arc.m_uuid;
        rad.m_distance = r;
        auto &rev = doc.insert_group<GroupRevolve>(UUID::random(), sk);
        rev.m_wrkpl = wrkpl;
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

} // namespace dune3d

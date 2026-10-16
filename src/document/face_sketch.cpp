#include "face_sketch.hpp"
#include "document/document.hpp"
#include "document/group/all_groups.hpp"
#include "document/group/igroup_solid_model.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_lock_rotation.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <TopoDS.hxx>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <map>

namespace dune3d {

std::optional<FacePick> pick_planar_face(const Document &doc, const UUID &upto, const glm::dvec3 &ray_origin,
                                         const glm::dvec3 &ray_dir)
{
    // only the last solid of each body is what's actually there
    std::map<UUID, const SolidModelOcc *> body_solids;
    for (auto gr : doc.get_groups_sorted()) {
        auto sm = dynamic_cast<const SolidModelOcc *>(IGroupSolidModel::try_get_solid_model(*gr));
        if (sm && !sm->m_shape_acc.IsNull())
            body_solids[gr->find_body(doc).group.m_uuid] = sm;
        if (gr->m_uuid == upto)
            break;
    }

    const gp_Lin ray{gp_Pnt(ray_origin.x, ray_origin.y, ray_origin.z), gp_Dir(ray_dir.x, ray_dir.y, ray_dir.z)};
    double best_w = std::numeric_limits<double>::infinity();
    TopoDS_Face best_face;
    for (const auto &[body, sm] : body_solids) {
        IntCurvesFace_ShapeIntersector inter;
        inter.Load(sm->m_shape_acc, 1e-6);
        inter.Perform(ray, -1e9, 1e9);
        if (!inter.IsDone())
            continue;
        for (int i = 1; i <= inter.NbPnt(); i++) {
            if (inter.WParameter(i) < best_w) {
                best_w = inter.WParameter(i);
                best_face = inter.Face(i);
            }
        }
    }
    if (best_face.IsNull())
        return {};

    BRepAdaptor_Surface surf{best_face};
    if (surf.GetType() != GeomAbs_Plane)
        return {};
    const auto pln = surf.Plane();
    gp_Dir n = pln.Axis().Direction();
    if (best_face.Orientation() == TopAbs_REVERSED)
        n.Reverse();
    const gp_Dir x = pln.XAxis().Direction();

    GProp_GProps props;
    BRepGProp::SurfaceProperties(best_face, props);
    const auto c = props.CentreOfMass();

    Bnd_Box box;
    BRepBndLib::Add(best_face, box);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);

    return FacePick{
            .centroid = {c.X(), c.Y(), c.Z()},
            .normal = {n.X(), n.Y(), n.Z()},
            .xdir = {x.X(), x.Y(), x.Z()},
            .bbox_min = {x0, y0, z0},
            .bbox_max = {x1, y1, z1},
    };
}

Group &add_sketch_on_face(Document &doc, const UUID &after, const FacePick &face)
{
    auto &sketch = doc.insert_group<GroupSketch>(UUID::random(), after);
    sketch.m_name = doc.find_next_group_name(Group::Type::SKETCH);

    const auto n = glm::normalize(face.normal);
    const auto x = glm::normalize(face.xdir - n * glm::dot(face.xdir, n));
    const auto y = glm::cross(n, x);
    const auto extent = face.bbox_max - face.bbox_min;
    const double size = std::max(glm::length(extent) * 1.2, 10.);
    const double tol = 1e-6 * std::max(1., size);

    auto &wrkpl = doc.add_entity<EntityWorkplane>(UUID::random());
    wrkpl.m_group = sketch.m_uuid;
    wrkpl.m_normal = glm::quat_cast(glm::dmat3(x, y, n));
    wrkpl.m_origin = face.centroid;
    wrkpl.m_size = {size, size};
    sketch.m_active_wrkpl = wrkpl.m_uuid;

    {
        auto &lock = doc.add_constraint<ConstraintLockRotation>(UUID::random());
        lock.m_group = sketch.m_uuid;
        lock.m_entity = wrkpl.m_uuid;
    }

    // pin the origin to the existing point on the face that's nearest its centre
    std::set<UUID> earlier_groups;
    for (auto gr : doc.get_groups_sorted()) {
        earlier_groups.insert(gr->m_uuid);
        if (gr->m_uuid == after)
            break;
    }
    std::optional<EntityAndPoint> anchor;
    glm::dvec3 anchor_pos;
    double anchor_dist = std::numeric_limits<double>::infinity();
    for (const auto &[uu, en] : doc.m_entities) {
        if (!earlier_groups.contains(en->m_group))
            continue;
        for (unsigned int pt = 0; pt < 4; pt++) {
            if (!en->is_valid_point(pt))
                continue;
            const auto p = en->get_point(pt, doc);
            if (std::abs(glm::dot(p - face.centroid, n)) > tol)
                continue;
            if (glm::any(glm::lessThan(p, face.bbox_min - tol)) || glm::any(glm::greaterThan(p, face.bbox_max + tol)))
                continue;
            const auto d = glm::length(p - face.centroid);
            if (d < anchor_dist) {
                anchor_dist = d;
                anchor = EntityAndPoint{uu, pt};
                anchor_pos = p;
            }
        }
    }
    if (anchor) {
        wrkpl.m_origin = anchor_pos;
        auto &co = doc.add_constraint<ConstraintPointsCoincident>(UUID::random());
        co.m_group = sketch.m_uuid;
        co.m_entity1 = {wrkpl.m_uuid, 1};
        co.m_entity2 = *anchor;
    }

    doc.set_group_generate_pending(sketch.m_uuid);
    return sketch;
}

} // namespace dune3d

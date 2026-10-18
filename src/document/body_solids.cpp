#include "body_solids.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/group/igroup_solid_model.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include "util/fs_util.hpp"
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <StlAPI_Writer.hxx>
#include <TopoDS_Compound.hxx>
#include "document/entity/entity_workplane.hpp"
#include "document/group/group_reference.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace dune3d {

std::vector<BodySolid> get_body_solids(const Document &doc, const UUID &upto, const BodyFilter &filter)
{
    std::map<UUID, const SolidModelOcc *> body_solids;
    for (auto gr : doc.get_groups_sorted()) {
        auto sm = dynamic_cast<const SolidModelOcc *>(IGroupSolidModel::try_get_solid_model(*gr));
        if (sm && !sm->m_shape_acc.IsNull())
            body_solids[gr->find_body(doc).group.m_uuid] = sm;
        if (gr->m_uuid == upto)
            break;
    }
    std::vector<BodySolid> r;
    for (const auto &[body, sm] : body_solids) {
        if (!filter || filter(body))
            r.push_back({body, sm});
    }
    return r;
}

bool grow_floor(Document &doc)
{
    double reach = 0; // how far from the origin any solid goes, along X or Y
    for (const auto &bs : get_body_solids(doc, UUID())) {
        for (const auto &face : bs.solid->m_faces) {
            for (const auto &v : face.vertices)
                reach = std::max({reach, std::abs((double)v.x), std::abs((double)v.y)});
        }
    }
    // some room around the objects, in steps of 100 mm so it doesn't change with every nudge
    const double half = std::ceil((reach + 50) / 100) * 100;
    auto &floor = doc.get_entity<EntityWorkplane>(doc.get_reference_group().get_workplane_xy_uuid());
    if (floor.m_size.x >= half * 2 && floor.m_size.y >= half * 2)
        return false;
    floor.m_size = glm::max(floor.m_size, glm::dvec2(half * 2));
    doc.get_reference_group().m_xy_size = floor.m_size;
    return true;
}

bool export_bodies_stl(const Document &doc, const std::filesystem::path &path)
{
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    bool any = false;
    // holes are only there to cut with
    for (const auto &bs :
         get_body_solids(doc, UUID(), [&doc](const UUID &b) { return !doc.get_group(b).m_body->m_hole; })) {
        builder.Add(compound, bs.solid->m_shape_acc);
        any = true;
    }
    if (!any)
        return false;
    // same settings as SolidModelOcc::export_stl
    BRepMesh_IncrementalMesh mesh(compound, 0.001, false, 0.5, true);
    StlAPI_Writer writer;
    return writer.Write(compound, path_to_string(path).c_str());
}

} // namespace dune3d

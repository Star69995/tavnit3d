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
#include <map>

namespace dune3d {

std::vector<BodySolid> get_body_solids(const Document &doc, const UUID &upto)
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
    for (const auto &[body, sm] : body_solids)
        r.push_back({body, sm});
    return r;
}

bool export_bodies_stl(const Document &doc, const std::filesystem::path &path)
{
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    bool any = false;
    for (const auto &bs : get_body_solids(doc, UUID())) {
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

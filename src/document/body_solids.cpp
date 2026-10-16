#include "body_solids.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/group/igroup_solid_model.hpp"
#include "document/solid_model/solid_model_occ.hpp"
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

} // namespace dune3d

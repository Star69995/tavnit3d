#include "solid_model.hpp"
#include "solid_model_util.hpp"
#include "solid_model_occ.hpp"
#include "document/group/group_sketch.hpp"
#include "document/document.hpp"
#include "document/entity/entity_step.hpp"
#include "import_step/imported_step.hpp"
#include "import_step/shapes.hpp"

#include <BRepBuilderAPI_Transform.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Quaternion.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>

namespace dune3d {

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupSketch &group)
{
    auto mod = std::make_shared<SolidModelOcc>();

    TopoDS_Shape fused;
    // meshes and loose faces can't be fused, they're just collected
    TopoDS_Compound loose;
    BRep_Builder builder;
    bool any_loose = false;
    for (const auto &[uu, en] : doc.m_entities) {
        if (en->m_group != group.m_uuid)
            continue;
        if (const auto step = dynamic_cast<const EntitySTEP *>(en.get())) {
            if (!step->m_include_in_solid_model)
                continue;

            if (auto shapes = step->m_imported->get_shapes()) {
                gp_Trsf trsf;
                const auto &org = step->m_origin;
                const auto &norm = step->m_normal;
                trsf.SetTranslation(gp_Vec(org.x, org.y, org.z));
                {
                    gp_Trsf trsf2;
                    trsf2.SetRotation(gp_Quaternion(norm.x, norm.y, norm.z, norm.w));
                    trsf.Multiply(trsf2);
                }

                const std::vector<TopoDS_Shape> *src = &shapes->shapes;
                if (step->m_part) {
                    if (*step->m_part >= shapes->parts.size())
                        continue;
                    src = &shapes->parts.at(*step->m_part).shapes;
                }
                if (step->m_part) {
                    for (const auto &[face, c] : shapes->parts.at(*step->m_part).face_colors)
                        mod->m_face_colors.emplace(face.TShape().get(), Color(c.at(0), c.at(1), c.at(2)));
                }
                for (const auto &shape : *src) {
                    if (TopExp_Explorer(shape, TopAbs_SOLID).More()) {
                        // imported objects are moved rather than copied, so their faces stay
                        // the same and keep their colours
                        const auto moved = step->m_part ? shape.Moved(TopLoc_Location(trsf))
                                                        : BRepBuilderAPI_Transform(shape, trsf, true).Shape();
                        TopExp_Explorer topex(moved, TopAbs_SOLID);
                        while (topex.More()) {
                            if (fused.IsNull())
                                fused = topex.Current();
                            else
                                fused = BRepAlgoAPI_Fuse(fused, topex.Current());
                            topex.Next();
                        }
                    }
                    else {
                        // moving keeps a mesh's triangles, copying wouldn't
                        if (!any_loose)
                            builder.MakeCompound(loose);
                        any_loose = true;
                        builder.Add(loose, shape.Moved(TopLoc_Location(trsf)));
                    }
                }
            }
        }
    }

    if (any_loose) {
        if (!fused.IsNull())
            builder.Add(loose, fused);
        fused = loose;
    }
    if (fused.IsNull())
        return nullptr;

    mod->m_shape = fused;

    if (!mod->update_acc_finish(doc, group))
        return nullptr;

    return mod;
}

} // namespace dune3d

#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "document/document.hpp"
#include "document/group/group_solid_model_operation.hpp"

#include <TopExp_Explorer.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <memory>

namespace dune3d {

namespace {
bool same_color(const Color &a, const Color &b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

// The boolean of two solids. Faces keep the colour of the solid they came from (a screen stays black on
// a silver body): the result's faces are looked up in the history of the operation and get a colour of
// their own where it differs from the argument's, which is also what the combined body is coloured with.
TopoDS_Shape calc_keep_colors(IGroupSolidModel::Operation op, const SolidModelOcc &argument,
                              const SolidModelOcc &tool, std::map<const TopoDS_TShape *, Color> &colors)
{
    std::unique_ptr<BRepAlgoAPI_BooleanOperation> algo;
    switch (op) {
    case IGroupSolidModel::Operation::DIFFERENCE:
        algo = std::make_unique<BRepAlgoAPI_Cut>(argument.m_shape_acc, tool.m_shape_acc);
        break;
    case IGroupSolidModel::Operation::UNION:
        algo = std::make_unique<BRepAlgoAPI_Fuse>(argument.m_shape_acc, tool.m_shape_acc);
        break;
    case IGroupSolidModel::Operation::INTERSECTION:
        algo = std::make_unique<BRepAlgoAPI_Common>(argument.m_shape_acc, tool.m_shape_acc);
        break;
    default:
        throw std::runtime_error("unsuppored solid model operation");
    }
    if (!algo->IsDone())
        return {};

    auto collect = [&](const SolidModelOcc &source, bool is_argument) {
        for (TopExp_Explorer ex(source.m_shape_acc, TopAbs_FACE); ex.More(); ex.Next()) {
            const auto &face = ex.Current();
            std::optional<Color> own;
            if (auto it = source.m_face_colors.find(face.TShape().get()); it != source.m_face_colors.end())
                own = it->second;
            // the argument's plain faces get the body's colour anyway
            if (!own && (is_argument || same_color(source.m_color, argument.m_color)))
                continue;
            if (algo->IsDeleted(face))
                continue;
            const auto &color = own ? *own : source.m_color;
            const auto &modified = algo->Modified(face);
            if (modified.IsEmpty()) {
                colors[face.TShape().get()] = color;
            }
            else {
                for (const auto &f : modified)
                    colors[f.TShape().get()] = color;
            }
        }
    };
    // the tool's faces only stay in the result of a join or an intersection; the argument wins where both are
    if (op != IGroupSolidModel::Operation::DIFFERENCE)
        collect(tool, false);
    collect(argument, true);
    return algo->Shape();
}
} // namespace

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupSolidModelOperation &group)
{
    auto &groups = doc.get_groups();
    group.m_solid_model_messages.clear();
    if (!groups.contains(group.m_source_group_argument)) {
        group.m_solid_model_messages.emplace_back(GroupStatusMessage::Status::ERR, "no argument source group");
        return nullptr;
    }
    if (!groups.contains(group.m_source_group_tool)) {
        group.m_solid_model_messages.emplace_back(GroupStatusMessage::Status::ERR, "no tool source group");
        return nullptr;
    }

    const auto source_group_argument =
            dynamic_cast<const IGroupSolidModel *>(groups.at(group.m_source_group_argument).get());
    if (!source_group_argument || !source_group_argument->get_solid_model()) {
        group.m_solid_model_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                  "argument source group has no solid model");
        return nullptr;
    }

    const auto source_group_tool = dynamic_cast<const IGroupSolidModel *>(groups.at(group.m_source_group_tool).get());
    if (!source_group_tool || !source_group_tool->get_solid_model()) {
        group.m_solid_model_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                  "tool source group has no solid model");
        return nullptr;
    }

    auto &solid_model_argument = dynamic_cast<const SolidModelOcc &>(*source_group_argument->get_solid_model());
    auto &solid_model_tool = dynamic_cast<const SolidModelOcc &>(*source_group_tool->get_solid_model());
    if (solid_model_argument.m_shape_acc.IsNull()) {
        group.m_solid_model_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                  "argument source group has no shape");
        return nullptr;
    }
    if (solid_model_tool.m_shape_acc.IsNull()) {
        group.m_solid_model_messages.emplace_back(GroupStatusMessage::Status::ERR, "tool source group has no shape");
        return nullptr;
    }

    auto mod = std::make_shared<SolidModelOcc>();

    mod->m_shape_acc =
            calc_keep_colors(group.m_operation, solid_model_argument, solid_model_tool, mod->m_face_colors);

    if (mod->m_shape_acc.IsNull()) {
        group.m_solid_model_messages.emplace_back(GroupStatusMessage::Status::ERR, "didn't generate a shape");
        return nullptr;
    }

    mod->finish(doc, group);

    return mod;
}

} // namespace dune3d

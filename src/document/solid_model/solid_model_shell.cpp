#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "document/document.hpp"
#include "document/group/group_shell.hpp"
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRepLProp_SLProps.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <sstream>

namespace dune3d {

// the face on the open side: facing up (or down) and the highest (lowest) of those
static TopoDS_Shape find_open_face(const TopoDS_Shape &shape, bool top)
{
    TopoDS_Shape best;
    double best_z = 0;
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
        const auto face = TopoDS::Face(ex.Current());
        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        if (props.Mass() < 1e-9)
            continue;
        const auto c = props.CentreOfMass();
        BRepAdaptor_Surface surf{face};
        double u0 = surf.FirstUParameter(), u1 = surf.LastUParameter();
        double v0 = surf.FirstVParameter(), v1 = surf.LastVParameter();
        BRepLProp_SLProps lp{surf, (u0 + u1) / 2, (v0 + v1) / 2, 1, 1e-6};
        if (!lp.IsNormalDefined())
            continue;
        auto n = lp.Normal();
        if (face.Orientation() == TopAbs_REVERSED)
            n.Reverse();
        const double nz = top ? n.Z() : -n.Z();
        if (nz < .7)
            continue;
        const double z = top ? c.Z() : -c.Z();
        if (best.IsNull() || z > best_z + 1e-6) {
            best = face;
            best_z = z;
        }
    }
    return best;
}

std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupShell &group)
{
    group.m_shell_messages.clear();
    const auto last_group = SolidModel::get_last_solid_model_group(doc, group);
    if (!last_group) {
        group.m_shell_messages.emplace_back(GroupStatusMessage::Status::ERR, "no solid model group");
        return nullptr;
    }
    group.m_operation = last_group->get_operation();
    const auto last = dynamic_cast<const SolidModelOcc *>(last_group->get_solid_model());
    if (!last || last->m_shape_acc.IsNull()) {
        group.m_shell_messages.emplace_back(GroupStatusMessage::Status::ERR, "no solid model");
        return nullptr;
    }
    if (group.m_thickness <= 0) {
        group.m_shell_messages.emplace_back(GroupStatusMessage::Status::ERR, "the wall needs a thickness");
        return nullptr;
    }

    auto mod = std::make_shared<SolidModelOcc>();
    try {
        TopTools_ListOfShape open;
        if (group.m_open != GroupShell::Open::NONE) {
            auto face = find_open_face(last->m_shape_acc, group.m_open == GroupShell::Open::TOP);
            if (face.IsNull()) {
                group.m_shell_messages.emplace_back(GroupStatusMessage::Status::WARN,
                                                    "no face to leave open there, hollow inside instead");
            }
            else {
                open.Append(face);
            }
        }
        BRepOffsetAPI_MakeThickSolid maker;
        // negative: the walls go inwards, the outside stays where it is
        maker.MakeThickSolidByJoin(last->m_shape_acc, open, -group.m_thickness, 1e-3, BRepOffset_Skin, false,
                                   false, GeomAbs_Arc);
        maker.Build();
        if (!maker.IsDone()) {
            group.m_shell_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                "couldn't hollow it out; try a thinner wall");
            return nullptr;
        }
        mod->m_shape_acc = maker.Shape();
        // it may "work" and still make nonsense, e.g. walls thicker than rounded edges are round
        if (!BRepCheck_Analyzer(mod->m_shape_acc).IsValid()) {
            group.m_shell_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                                "couldn't hollow it out; try a thinner wall (thinner than any "
                                                "rounding of the edges)");
            return nullptr;
        }
    }
    catch (const Standard_Failure &e) {
        std::ostringstream os;
        e.Print(os);
        group.m_shell_messages.emplace_back(GroupStatusMessage::Status::ERR,
                                            "couldn't hollow it out (" + os.str() + "); try a thinner wall");
        return nullptr;
    }
    if (mod->m_shape_acc.IsNull())
        return nullptr;
    mod->finish(doc, group);
    return mod;
}

} // namespace dune3d

#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "document/document.hpp"
#include "document/group/group_thread.hpp"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <Bnd_Box.hxx>
#include <Geom2d_Line.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <TopTools_ListOfShape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Trsf.hxx>
#include <cmath>
#include <numbers>
#include <sstream>

namespace dune3d {

// A triangle-ish ridge swept along a helix on the root cylinder (binormal fixed to
// the axis so the profile stays in the radial plane), fused to the solid, then the
// start cut to a cone so the tip enters a nut. The ridge's foot is sunk into the
// core; cutting the ridge itself first makes the fuse silently return the core only.
std::shared_ptr<const SolidModel> SolidModel::create(const Document &doc, GroupThread &group)
{
    group.m_thread_messages.clear();
    const auto last_group = SolidModel::get_last_solid_model_group(doc, group);
    if (!last_group) {
        group.m_thread_messages.emplace_back(GroupStatusMessage::Status::ERR, "no solid model group");
        return nullptr;
    }
    group.m_operation = last_group->get_operation();
    const auto last = dynamic_cast<const SolidModelOcc *>(last_group->get_solid_model());
    if (!last || last->m_shape_acc.IsNull()) {
        group.m_thread_messages.emplace_back(GroupStatusMessage::Status::ERR, "no solid model");
        return nullptr;
    }
    const double pitch = group.m_pitch, depth = group.m_depth, rmin = group.m_root_radius;
    if (pitch <= 0 || depth <= 0 || rmin <= 0) {
        group.m_thread_messages.emplace_back(GroupStatusMessage::Status::ERR, "the thread needs a pitch and depth");
        return nullptr;
    }

    auto mod = std::make_shared<SolidModelOcc>();
    try {
        Bnd_Box box;
        BRepBndLib::AddOptimal(last->m_shape_acc, box, false, false); // the plain one is loose on curved faces
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        const gp_Pnt base((x0 + x1) / 2, (y0 + y1) / 2, z0);
        const double rmaj = rmin + depth;
        const double length = std::min(group.m_length, z1 - z0);
        const double span = length - .9 * pitch; // the profile is .9 pitch wide at the foot
        if (span < pitch) {
            group.m_thread_messages.emplace_back(GroupStatusMessage::Status::ERR, "too short for a thread");
            return nullptr;
        }
        const double turns = span / pitch;
        const double len = turns * std::sqrt(4 * std::numbers::pi * std::numbers::pi + pitch * pitch);
        Handle(Geom_CylindricalSurface) cyl = new Geom_CylindricalSurface(gp_Ax3(), rmin);
        Handle(Geom2d_Line) ln = new Geom2d_Line(gp_Pnt2d(0, 0), gp_Dir2d(2 * std::numbers::pi, pitch));
        TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(ln, cyl, 0, len);
        BRepLib::BuildCurves3d(edge);
        TopoDS_Wire spine = BRepBuilderAPI_MakeWire(edge);

        const double sunk = .15 * depth;
        BRepBuilderAPI_MakePolygon prof;
        prof.Add(gp_Pnt(rmin - sunk, 0, -.45 * pitch));
        prof.Add(gp_Pnt(rmaj, 0, -.05 * pitch));
        prof.Add(gp_Pnt(rmaj, 0, .05 * pitch));
        prof.Add(gp_Pnt(rmin - sunk, 0, .45 * pitch));
        prof.Close();
        BRepOffsetAPI_MakePipeShell pipe(spine);
        pipe.SetMode(gp_Dir(0, 0, 1));
        pipe.Add(prof.Wire());
        pipe.Build();
        if (!pipe.IsDone() || !pipe.MakeSolid()) {
            group.m_thread_messages.emplace_back(GroupStatusMessage::Status::ERR, "couldn't wind the thread");
            return nullptr;
        }
        TopoDS_Shape ridge = pipe.Shape();
        gp_Trsf up;
        up.SetTranslation(gp_Vec(base.X(), base.Y(), base.Z() + .45 * pitch)); // the first turn starts at the bottom
        ridge.Move(TopLoc_Location(up));

        BRepAlgoAPI_Fuse fuse;
        {
            TopTools_ListOfShape a, b;
            a.Append(last->m_shape_acc);
            b.Append(ridge);
            fuse.SetArguments(a);
            fuse.SetTools(b);
            fuse.Build();
        }
        if (fuse.HasErrors() || fuse.Shape().IsNull()) {
            group.m_thread_messages.emplace_back(GroupStatusMessage::Status::ERR, "couldn't join the thread to the screw");
            return nullptr;
        }
        TopoDS_Shape result = fuse.Shape();

        // the tip: remove everything outside a cone growing from a bit under the root to the crest
        const double ch = std::min(1.2 * pitch, length / 2), tip_r = rmin * .85, big = rmaj * 1.5;
        auto cone_r = [&](double z) { return tip_r + (rmaj - tip_r) * z / ch; };
        BRepBuilderAPI_MakePolygon cp;
        cp.Add(gp_Pnt(cone_r(-.5), 0, -.5));
        cp.Add(gp_Pnt(big, 0, -.5));
        cp.Add(gp_Pnt(big, 0, ch));
        cp.Add(gp_Pnt(rmaj, 0, ch));
        cp.Close();
        TopoDS_Shape cutter = BRepPrimAPI_MakeRevol(BRepBuilderAPI_MakeFace(cp.Wire()), gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)));
        gp_Trsf to_base;
        to_base.SetTranslation(gp_Vec(base.X(), base.Y(), base.Z()));
        cutter.Move(TopLoc_Location(to_base));
        BRepAlgoAPI_Cut cut(result, cutter);
        if (!cut.HasErrors() && !cut.Shape().IsNull())
            result = cut.Shape();

        mod->m_shape_acc = result;
        if (!BRepCheck_Analyzer(mod->m_shape_acc).IsValid()) {
            group.m_thread_messages.emplace_back(GroupStatusMessage::Status::ERR, "the thread came out broken");
            return nullptr;
        }
    }
    catch (const Standard_Failure &e) {
        std::ostringstream os;
        e.Print(os);
        group.m_thread_messages.emplace_back(GroupStatusMessage::Status::ERR, "couldn't make the thread (" + os.str() + ")");
        return nullptr;
    }
    mod->finish(doc, group);
    return mod;
}

} // namespace dune3d

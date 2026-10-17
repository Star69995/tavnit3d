// Builds each ready-made primitive in a fresh document and checks that its
// sketch is fully constrained, it solves cleanly and the resulting solid
// has the expected volume.
#include "document/document.hpp"
#include "nlohmann/json.hpp"
#include "document/primitives.hpp"
#include "document/face_sketch.hpp"
#include "document/body_solids.hpp"
#include "document/combine.hpp"
#include "document/duplicate.hpp"
#include "document/round_edges.hpp"
#include "document/group/group_fillet.hpp"
#include <filesystem>
#include <fstream>
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_point_distance.hpp"
#include "document/group/group.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include "preferences/preferences.hpp"
#include <BRepGProp.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include "document/constraint/constraint_point_distance_hv.hpp"
#include "document/constraint/constraint_diameter_radius.hpp"
#include "document/entity/entity.hpp"
#include <GProp_GProps.hxx>
#include <cmath>
#include <iostream>
#include <sstream>
#include <numbers>

using namespace dune3d;

// the core reads a few defaults from the preferences; tests use the built-in ones
const Preferences &Preferences::get()
{
    static Preferences the_preferences;
    return the_preferences;
}

static int failures = 0;

static void check(bool ok, const std::string &what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok)
        failures++;
}

static void test_primitive(const char *name, Primitive primitive, double expected_volume)
{
    std::cout << name << std::endl;
    Document doc;
    const auto first = doc.get_groups_sorted().back()->m_uuid;
    auto &group = add_primitive(doc, first, primitive);
    doc.update_pending();

    for (auto gr : doc.get_groups_sorted()) {
        check(gr->m_solve_result == SolveResult::OKAY, std::string("group '") + gr->m_name + "' solves");
        if (gr->get_type() == Group::Type::SKETCH && gr->m_uuid != first)
            check(gr->m_dof == 0, std::string("sketch '") + gr->m_name
                                          + "' fully constrained (dof=" + std::to_string(gr->m_dof) + ")");
    }

    auto sm = dynamic_cast<const SolidModelOcc *>(
            SolidModel::get_last_solid_model(doc, group, SolidModel::IncludeGroup::YES));
    check(sm != nullptr, "has solid model");
    if (!sm)
        return;
    check(!sm->m_shape_acc.IsNull(), "solid model is not empty");
    if (sm->m_shape_acc.IsNull())
        return;
    GProp_GProps props;
    BRepGProp::VolumeProperties(sm->m_shape_acc, props);
    const auto vol = props.Mass();
    check(std::abs(vol - expected_volume) / expected_volume < 1e-3,
          "volume " + std::to_string(vol) + " ~ " + std::to_string(expected_volume));
}

static bool near(const glm::dvec3 &a, const glm::dvec3 &b)
{
    return glm::length(a - b) < 1e-6;
}

static void test_sketch_on_face()
{
    std::cout << "sketch on face" << std::endl;
    {
        Document doc;
        auto &cyl = add_primitive(doc, doc.get_groups_sorted().back()->m_uuid, Primitive::CYLINDER);
        doc.update_pending();
        auto top = pick_planar_face(doc, cyl.m_uuid, {3, 0, 100}, {0, 0, -1});
        check(top && near(top->normal, {0, 0, 1}), "cylinder top is flat");
        auto round = pick_planar_face(doc, cyl.m_uuid, {0, 100, 5}, {0, -1, 0});
        check(!round, "cylinder side is curved, not picked");
    }

    Document doc;
    auto &box = add_primitive(doc, doc.get_groups_sorted().back()->m_uuid, Primitive::BOX);
    doc.update_pending();

    auto top = pick_planar_face(doc, box.m_uuid, {3, 4, 100}, {0, 0, -1});
    check(top.has_value(), "ray from above hits a flat face");
    if (!top)
        return;
    check(near(top->normal, {0, 0, 1}), "top face normal points up");
    check(std::abs(top->centroid.z - 20) < 1e-6, "top face is at z=20");

    auto bottom = pick_planar_face(doc, box.m_uuid, {3, 4, -100}, {0, 0, 1});
    check(bottom && near(bottom->normal, {0, 0, -1}), "bottom face normal points down");

    auto side = pick_planar_face(doc, box.m_uuid, {100, 2, 5}, {-1, 0, 0});
    check(side && near(side->normal, {1, 0, 0}), "side face normal points along +x");

    auto miss = pick_planar_face(doc, box.m_uuid, {500, 500, 100}, {0, 0, -1});
    check(!miss, "ray past everything picks nothing");

    auto &sketch = add_sketch_on_face(doc, box.m_uuid, *top);
    doc.update_pending();
    check(sketch.m_solve_result == SolveResult::OKAY, "face sketch solves");
    check(sketch.m_dof == 0, "face sketch workplane is pinned (dof=" + std::to_string(sketch.m_dof) + ")");
    auto &wrkpl = doc.get_entity<EntityWorkplane>(sketch.m_active_wrkpl);
    check(std::abs(wrkpl.m_origin.z - 20) < 1e-6, "workplane sits on the top face");
    check(near(wrkpl.get_normal_vector(), {0, 0, 1}), "workplane faces up");

    // make the box taller: the face sketch should move along
    for (auto &[uu, co] : doc.m_constraints) {
        if (co->m_group != box.m_uuid)
            continue;
        if (auto dist = dynamic_cast<ConstraintPointDistance *>(co.get()))
            dist->m_distance = 30;
    }
    doc.set_group_solve_pending(box.m_uuid);
    doc.update_pending();
    const auto z = doc.get_entity<EntityWorkplane>(sketch.m_active_wrkpl).m_origin.z;
    check(std::abs(z - 30) < 1e-6, "workplane follows the face when the box gets taller (z=" + std::to_string(z) + ")");
}

static Bnd_Box solid_bbox(const Document &doc, const Group &group)
{
    Bnd_Box box;
    auto sm = dynamic_cast<const SolidModelOcc *>(
            SolidModel::get_last_solid_model(doc, group, SolidModel::IncludeGroup::YES));
    if (sm && !sm->m_shape_acc.IsNull())
        BRepBndLib::AddOptimal(sm->m_shape_acc, box, false, false);
    return box;
}

static void test_placement()
{
    std::cout << "placement" << std::endl;
    Document doc;
    auto &sphere = add_primitive(doc, doc.get_groups_sorted().back()->m_uuid, Primitive::SPHERE);
    doc.update_pending();
    {
        double x0, y0, z0, x1, y1, z1;
        solid_bbox(doc, sphere).Get(x0, y0, z0, x1, y1, z1);
        check(std::abs(z0) < 1e-3 && std::abs(z1 - 20) < 1e-3,
              "sphere rests on the plane (z " + std::to_string(z0) + ".." + std::to_string(z1) + ")");
    }

    const auto spot = find_free_spot(doc, sphere.m_uuid);
    check(spot.x >= 20, "free spot is clear of the sphere (x=" + std::to_string(spot.x) + ")");
    auto &box = add_primitive(doc, sphere.m_uuid, Primitive::BOX, spot);
    doc.update_pending();
    double x0, y0, z0, x1, y1, z1;
    solid_bbox(doc, box).Get(x0, y0, z0, x1, y1, z1);
    check(x0 > 10 + 1e-3, "box placed next to the sphere doesn't overlap it (x0=" + std::to_string(x0) + ")");
    check(std::abs((x0 + x1) / 2 - spot.x) < 1e-3, "box is centred on the spot");
    for (auto gr : doc.get_groups_sorted()) {
        if (gr->get_type() == Group::Type::SKETCH && gr->m_body)
            check(gr->m_dof == 0, "sketch '" + gr->m_name + "' fully constrained");
    }

    // moving it: the X position is a horizontal distance from the workplane origin
    const auto box_sketch = doc.get_group_rel(box.m_uuid, -1);
    for (auto &[uu, co] : doc.m_constraints) {
        auto h = dynamic_cast<ConstraintPointDistanceHorizontal *>(co.get());
        if (h && co->m_group == box_sketch && doc.get_entity(h->m_entity1.entity).get_type() == Entity::Type::WORKPLANE)
            h->m_distance = -50;
    }
    doc.set_group_solve_pending(box_sketch);
    doc.update_pending();
    solid_bbox(doc, box).Get(x0, y0, z0, x1, y1, z1);
    check(std::abs((x0 + x1) / 2 + 50) < 1e-3, "changing X moves the box (centre x=" + std::to_string((x0 + x1) / 2) + ")");
}

static void test_export_stl()
{
    std::cout << "export all as STL" << std::endl;
    Document doc;
    check(!export_bodies_stl(doc, std::filesystem::temp_directory_path() / "dune3d-empty.stl"),
          "nothing to export in an empty document");
    auto &box = add_primitive(doc, doc.get_groups_sorted().back()->m_uuid, Primitive::BOX);
    add_primitive(doc, box.m_uuid, Primitive::CYLINDER, {40, 0});
    doc.update_pending();
    const auto path = std::filesystem::temp_directory_path() / "dune3d-test-all.stl";
    check(export_bodies_stl(doc, path), "export succeeds");
    std::ifstream ifs(path);
    std::string line;
    unsigned int facets = 0;
    double max_x = -1e9;
    while (std::getline(ifs, line)) {
        if (line.find("facet normal") != std::string::npos)
            facets++;
        if (auto i = line.find("vertex"); i != std::string::npos) {
            std::istringstream iss(line.substr(i + 6));
            double x;
            iss >> x;
            max_x = std::max(max_x, x);
        }
    }
    check(facets > 12, "STL has triangles (" + std::to_string(facets) + ")");
    check(std::abs(max_x - 50) < 1e-3, "STL contains both objects (max x " + std::to_string(max_x) + ")");
    ifs.close();
    std::filesystem::remove(path);
}

static double body_volume(const Document &doc, const Group &group)
{
    auto sm = dynamic_cast<const SolidModelOcc *>(
            SolidModel::get_last_solid_model(doc, group, SolidModel::IncludeGroup::YES));
    if (!sm || sm->m_shape_acc.IsNull())
        return -1;
    GProp_GProps props;
    BRepGProp::VolumeProperties(sm->m_shape_acc, props);
    return props.Mass();
}

// moves a body's X/Y position (its distances from the workplane origin) and re-solves
static void set_position(Document &doc, const Group &solid, double x, double y)
{
    const auto sketch = doc.get_group_rel(solid.m_uuid, -1);
    for (auto &[uu, co] : doc.m_constraints) {
        if (co->m_group != sketch)
            continue;
        auto from_origin = [&doc](const EntityAndPoint &e) {
            return doc.get_entity(e.entity).get_type() == Entity::Type::WORKPLANE;
        };
        if (auto h = dynamic_cast<ConstraintPointDistanceHorizontal *>(co.get()); h && from_origin(h->m_entity1))
            h->m_distance = x;
        if (auto v = dynamic_cast<ConstraintPointDistanceVertical *>(co.get()); v && from_origin(v->m_entity1))
            v->m_distance = y;
    }
    doc.set_group_solve_pending(sketch);
    doc.update_pending();
}

// outlines and revolved profiles move as a whole with their X/Y position, and a
// full revolve stays a full turn
static void test_moving_shapes()
{
    std::cout << "moving outlines and revolves" << std::endl;
    Document doc;
    auto &outline = add_extruded_outline(doc, doc.get_groups_sorted().back()->m_uuid,
                                         {{-5, -5}, {5, -5}, {5, 5}, {-5, 5}}, 4, false, {10, 0});
    doc.update_pending();
    set_position(doc, outline, 30, 20);
    {
        double x0, y0, z0, x1, y1, z1;
        solid_bbox(doc, outline).Get(x0, y0, z0, x1, y1, z1);
        check(std::abs((x0 + x1) / 2 - 30) < 1e-3 && std::abs((y0 + y1) / 2 - 20) < 1e-3,
              "outline follows X/Y (centre " + std::to_string((x0 + x1) / 2) + ", " + std::to_string((y0 + y1) / 2)
                      + ")");
        check(std::abs(body_volume(doc, outline) - 400) < 1e-3, "outline keeps its shape");
    }

    auto &rev = add_revolved_profile(doc, doc.get_groups_sorted().back()->m_uuid, {{0, 0}, {10, 0}, {10, 10}, {0, 10}},
                                     false, {-30, 0});
    doc.update_pending();
    // one move after another, as from the panel: X, then Y
    set_position(doc, rev, 50, 0);
    set_position(doc, rev, 50, 25);
    {
        double x0, y0, z0, x1, y1, z1;
        solid_bbox(doc, rev).Get(x0, y0, z0, x1, y1, z1);
        check(std::abs((y0 + y1) / 2 - 25) < 1e-3, "revolve follows Y (centre y " + std::to_string((y0 + y1) / 2) + ")");
        const double expected = std::numbers::pi * 100 * 10;
        check(std::abs(body_volume(doc, rev) - expected) / expected < 1e-3,
              "revolve is still a full turn (volume " + std::to_string(body_volume(doc, rev)) + ")");
    }

    // the same with a smooth profile, like the dome
    std::vector<glm::dvec2> quarter{{0, 0}};
    for (int i = 0; i <= 5; i++)
        quarter.push_back({20 * std::cos(i * std::numbers::pi / 10), 20 * std::sin(i * std::numbers::pi / 10)});
    auto &dome = add_revolved_profile(doc, doc.get_groups_sorted().back()->m_uuid, quarter, true, {-60, 0});
    doc.update_pending();
    const auto dome_volume = body_volume(doc, dome);
    set_position(doc, dome, -60, -35);
    set_position(doc, dome, -20, -35);
    check(dome_volume > 0 && std::abs(body_volume(doc, dome) - dome_volume) / dome_volume < 1e-3,
          "smooth revolve is still a full turn (volume " + std::to_string(body_volume(doc, dome)) + " was "
                  + std::to_string(dome_volume) + ")");
}

static void test_more_shapes()
{
    std::cout << "more shapes" << std::endl;
    constexpr double pi = std::numbers::pi;
    const struct {
        const char *name;
        Primitive prim;
        double volume;
    } cases[] = {
            {"cone r10 h20", Primitive::CONE, pi * 100 * 20 / 3},
            {"tube 20/12 h20", Primitive::TUBE, pi * (100 - 36) * 20},
            {"torus 10/4", Primitive::TORUS, 2 * pi * pi * 10 * 16},
            {"wedge 20x20x20", Primitive::WEDGE, 20. * 20 / 2 * 20},
    };
    const glm::dvec2 pos{30, -10};
    for (const auto &c : cases) {
        Document doc;
        auto &group = add_primitive(doc, doc.get_groups_sorted().back()->m_uuid, c.prim, pos);
        doc.update_pending();
        for (auto gr : doc.get_groups_sorted()) {
            check(gr->m_solve_result == SolveResult::OKAY, std::string(c.name) + ": '" + gr->m_name + "' solves");
            if (gr->get_type() == Group::Type::SKETCH && gr->m_body && gr->get_index() > 1)
                check(gr->m_dof == 0, std::string(c.name) + ": fully constrained (dof=" + std::to_string(gr->m_dof) + ")");
        }
        const auto vol = body_volume(doc, group);
        check(std::abs(vol - c.volume) / c.volume < 2e-3,
              std::string(c.name) + ": volume " + std::to_string(vol) + " ~ " + std::to_string(c.volume));
        double x0, y0, z0, x1, y1, z1;
        auto box = solid_bbox(doc, group);
        if (box.IsVoid())
            continue;
        box.Get(x0, y0, z0, x1, y1, z1);
        check(std::abs(z0) < 1e-3, std::string(c.name) + ": rests on the plane (z0=" + std::to_string(z0) + ")");
        check(std::abs((x0 + x1) / 2 - pos.x) < 1e-3 && std::abs((y0 + y1) / 2 - pos.y) < 1e-3,
              std::string(c.name) + ": centred on its position (" + std::to_string((x0 + x1) / 2) + ", "
                      + std::to_string((y0 + y1) / 2) + ")");
    }
}

static void test_duplicate()
{
    std::cout << "duplicate" << std::endl;
    for (auto prim : {Primitive::BOX, Primitive::CYLINDER, Primitive::SPHERE, Primitive::TORUS, Primitive::WEDGE}) {
        Document doc;
        auto &orig = add_primitive(doc, doc.get_groups_sorted().back()->m_uuid, prim);
        doc.update_pending();
        const auto orig_body = orig.find_body(doc).group.m_uuid;
        const auto orig_name = doc.get_group(orig_body).m_body->m_name;
        doc.get_group(orig_body).m_body->m_material = Body::Material::METAL; // body settings travel along
        const auto orig_vol = body_volume(doc, orig);
        auto &copy_first = duplicate_body(doc, orig_body, {40, 0});
        doc.update_pending();
        const auto copy_last = doc.get_groups_sorted().back();
        const auto label = orig_name + ": ";
        check(copy_first.m_body && copy_first.m_body->m_name == orig_name + " copy", label + "copy is a new body");
        for (auto gr : doc.get_groups_sorted()) {
            check(gr->m_solve_result == SolveResult::OKAY, label + "'" + gr->m_name + "' solves");
            if (gr->get_type() == Group::Type::SKETCH && gr->m_body && gr->get_index() > 1)
                check(gr->m_dof == 0, label + "'" + gr->m_name + "' fully constrained (dof=" + std::to_string(gr->m_dof) + ")");
        }
        const auto vol = body_volume(doc, *copy_last);
        check(std::abs(vol - orig_vol) / orig_vol < 1e-3, label + "same volume " + std::to_string(vol));
        double x0, y0, z0, x1, y1, z1;
        solid_bbox(doc, *copy_last).Get(x0, y0, z0, x1, y1, z1);
        check(std::abs((x0 + x1) / 2 - 40) < 1e-3, label + "moved by the offset (x=" + std::to_string((x0 + x1) / 2) + ")");

        // the copy's dimensions are its own
        for (auto &[uu, co] : doc.m_constraints) {
            if (co->m_group == copy_first.m_uuid) {
                if (auto d = dynamic_cast<ConstraintDiameterRadius *>(co.get()))
                    d->m_distance *= 1.5;
            }
        }
        doc.set_group_solve_pending(copy_first.m_uuid);
        doc.update_pending();
        check(std::abs(body_volume(doc, orig) - orig_vol) / orig_vol < 1e-3, label + "original unchanged by edits to the copy");
    }
}

static void test_save_load()
{
    std::cout << "save and load" << std::endl;
    Document doc;
    auto &box = add_primitive(doc, doc.get_groups_sorted().back()->m_uuid, Primitive::BOX);
    auto &torus = add_primitive(doc, box.m_uuid, Primitive::TORUS, {40, 0});
    doc.update_pending();
    doc.get_group(torus.find_body(doc).group.m_uuid).m_body->m_material = Body::Material::GLOSSY;
    const auto j = doc.serialize();
    Document loaded{j, std::filesystem::temp_directory_path()};
    check(loaded.m_entities.size() == doc.m_entities.size(),
          "entities survive (" + std::to_string(loaded.m_entities.size()) + "/" + std::to_string(doc.m_entities.size()) + ")");
    check(loaded.m_constraints.size() == doc.m_constraints.size(), "constraints survive");
    check(loaded.get_groups().size() == doc.get_groups().size(), "groups survive");
    auto &ltorus = loaded.get_group(torus.m_uuid);
    check(std::abs(body_volume(loaded, ltorus) - body_volume(doc, torus)) < 1e-3, "same solid after loading");
    check(loaded.get_group(ltorus.find_body(loaded).group.m_uuid).m_body->m_material == Body::Material::GLOSSY,
          "material survives");
}

static void test_round_edges()
{
    std::cout << "round edges" << std::endl;
    for (auto [prim, name, expect] : std::vector<std::tuple<Primitive, const char *, bool>>{
                 {Primitive::BOX, "box", true},
                 {Primitive::CYLINDER, "cylinder", true},
                 {Primitive::WEDGE, "wedge", true},
                 {Primitive::SPHERE, "sphere", false},
         }) {
        Document doc;
        auto &shape = add_primitive(doc, doc.get_groups_sorted().back()->m_uuid, prim);
        doc.update_pending();
        const auto body = shape.find_body(doc).group.m_uuid;
        const auto vol0 = body_volume(doc, shape);
        auto fillet = set_round_edges(doc, body, 2);
        doc.update_pending();
        check((fillet != nullptr) == expect, std::string(name) + (expect ? ": edges get rounded" : ": nothing to round"));
        if (!fillet)
            continue;
        const auto vol = body_volume(doc, *fillet);
        check(vol > 0 && vol < vol0 && vol > vol0 * .8,
              std::string(name) + ": smaller but similar volume (" + std::to_string(vol) + " < " + std::to_string(vol0) + ")");
        check(find_round_edges(doc, body) == fillet, std::string(name) + ": found again");
        set_round_edges(doc, body, 3);
        doc.update_pending();
        check(body_volume(doc, *fillet) < vol, std::string(name) + ": bigger radius, less volume");
        set_round_edges(doc, body, 0);
        doc.update_pending();
        check(!find_round_edges(doc, body) && std::abs(body_volume(doc, shape) - vol0) < 1e-3,
              std::string(name) + ": radius 0 takes it away");
    }
}

static void test_combine()
{
    std::cout << "combine" << std::endl;
    constexpr double pi = std::numbers::pi;
    const double half_cylinder = pi * 10 * 10 / 2 * 20; // the part of the cylinder inside the box
    const struct {
        IGroupSolidModel::Operation op;
        const char *name;
        double volume;
    } cases[] = {
            {IGroupSolidModel::Operation::UNION, "join", 8000 + pi * 100 * 20 - half_cylinder},
            {IGroupSolidModel::Operation::DIFFERENCE, "subtract", 8000 - half_cylinder},
            {IGroupSolidModel::Operation::INTERSECTION, "intersect", half_cylinder},
    };
    for (const auto &c : cases) {
        Document doc;
        auto &box = add_primitive(doc, doc.get_groups_sorted().back()->m_uuid, Primitive::BOX);
        auto &cyl = add_primitive(doc, box.m_uuid, Primitive::CYLINDER, {10, 0});
        doc.update_pending();
        const auto box_body = box.find_body(doc).group.m_uuid;
        const auto cyl_body = cyl.find_body(doc).group.m_uuid;
        auto &comb = add_combination(doc, box_body, cyl_body, c.op);
        doc.update_pending();
        const auto vol = body_volume(doc, comb);
        check(std::abs(vol - c.volume) / c.volume < 1e-3,
              std::string(c.name) + " volume " + std::to_string(vol) + " ~ " + std::to_string(c.volume));
        check(comb.m_body.has_value() && comb.find_body(doc).group.m_uuid == comb.m_uuid, std::string(c.name) + " is a new body");
    }
}

int main()
{
    constexpr double pi = std::numbers::pi;
    test_moving_shapes();
    test_primitive("box 20x20x20", Primitive::BOX, 20. * 20. * 20.);
    test_primitive("cylinder r10 h20", Primitive::CYLINDER, pi * 10. * 10. * 20.);
    test_primitive("sphere r10", Primitive::SPHERE, 4. / 3. * pi * 10. * 10. * 10.);
    test_sketch_on_face();
    test_placement();
    test_export_stl();
    test_combine();
    test_more_shapes();
    test_duplicate();
    test_save_load();
    test_round_edges();
    std::cout << (failures ? "FAILED" : "PASSED") << " (" << failures << " failures)" << std::endl;
    return failures ? 1 : 0;
}

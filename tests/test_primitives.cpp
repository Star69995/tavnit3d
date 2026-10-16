// Builds each ready-made primitive in a fresh document and checks that its
// sketch is fully constrained, it solves cleanly and the resulting solid
// has the expected volume.
#include "document/document.hpp"
#include "document/primitives.hpp"
#include "document/face_sketch.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_point_distance.hpp"
#include "document/group/group.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include "preferences/preferences.hpp"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <iostream>
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

int main()
{
    constexpr double pi = std::numbers::pi;
    test_primitive("box 20x20x20", Primitive::BOX, 20. * 20. * 20.);
    test_primitive("cylinder r10 h20", Primitive::CYLINDER, pi * 10. * 10. * 20.);
    test_primitive("sphere r10", Primitive::SPHERE, 4. / 3. * pi * 10. * 10. * 10.);
    test_sketch_on_face();
    std::cout << (failures ? "FAILED" : "PASSED") << " (" << failures << " failures)" << std::endl;
    return failures ? 1 : 0;
}

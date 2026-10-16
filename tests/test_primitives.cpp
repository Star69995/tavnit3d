// Builds each ready-made primitive in a fresh document and checks that its
// sketch is fully constrained, it solves cleanly and the resulting solid
// has the expected volume.
#include "document/document.hpp"
#include "document/primitives.hpp"
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

int main()
{
    constexpr double pi = std::numbers::pi;
    test_primitive("box 20x20x20", Primitive::BOX, 20. * 20. * 20.);
    test_primitive("cylinder r10 h20", Primitive::CYLINDER, pi * 10. * 10. * 20.);
    test_primitive("sphere r10", Primitive::SPHERE, 4. / 3. * pi * 10. * 10. * 10.);
    std::cout << (failures ? "FAILED" : "PASSED") << " (" << failures << " failures)" << std::endl;
    return failures ? 1 : 0;
}

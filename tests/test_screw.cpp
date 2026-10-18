// Builds every screw head type at a few sizes and checks the recipe goes
// through without warnings and gives one solid of a plausible volume.
#include "document/document.hpp"
#include "document/recipe.hpp"
#include "document/screw.hpp"
#include "nlohmann/json.hpp"
#include "document/group/group.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include "preferences/preferences.hpp"
#include <BRepGProp.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>

using namespace dune3d;

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

static void test_screw(const ScrewParams &p, const char *head_name)
{
    const auto label = std::string(head_name) + " " + screw_label(p) + (p.thread ? "" : " plain");
    std::cout << label << std::endl;
    Document doc;
    const auto t0 = std::chrono::steady_clock::now();
    const auto result = build_recipe(doc, doc.get_groups_sorted().back()->m_uuid, make_screw_recipe(p));
    doc.update_pending();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    for (const auto &w : result.warnings)
        std::cout << "  warning: " << w << std::endl;
    check(result.warnings.empty(), "no warnings");
    check(!result.bodies.empty(), "made a body");
    if (result.bodies.empty())
        return;
    const auto &last = doc.get_group(result.bodies.back());
    auto sm = dynamic_cast<const SolidModelOcc *>(SolidModel::get_last_solid_model(doc, last, SolidModel::IncludeGroup::YES));
    check(sm && !sm->m_shape_acc.IsNull(), "has a solid");
    if (!sm || sm->m_shape_acc.IsNull())
        return;
    GProp_GProps props;
    BRepGProp::VolumeProperties(sm->m_shape_acc, props);
    const double r = p.diameter / 2;
    const double pin = std::numbers::pi * r * r * p.length;
    // thread ridges make it a bit thinner than the full pin; the head adds a lot
    check(props.Mass() > pin * .7, "volume " + std::to_string(props.Mass()) + " vs pin " + std::to_string(pin));
    Bnd_Box box;
    BRepBndLib::AddOptimal(sm->m_shape_acc, box, false, false);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    std::cout << "  z " << z0 << " .. " << z1 << std::endl;
    check(std::abs(z0) < 1e-2 && z1 > p.length, "stands on z = 0 and the head is above the shank");
    check(x1 - x0 > p.diameter, "head is wider than the shank");
    std::cout << "  built in " << ms << " ms" << std::endl;
    check(ms < 20000, "builds in reasonable time");
}

int main()
{
    test_screw({ScrewHead::HEX, 8, 30, true}, "hex");
    test_screw({ScrewHead::HEX, 8, 30, false}, "hex");
    test_screw({ScrewHead::CAP, 6, 20, true}, "cap");
    test_screw({ScrewHead::PAN, 4, 12, true}, "pan");
    test_screw({ScrewHead::COUNTERSUNK, 5, 25, true}, "countersunk");
    test_screw({ScrewHead::HEX, 10, 80, true}, "hex long");
    test_screw({ScrewHead::CAP, 7, 18, true}, "cap nonstandard");
    check(std::abs(screw_pitch(8) - 1.25) < 1e-9, "M8 pitch 1.25");
    check(screw_label({ScrewHead::HEX, 8, 30, true}) == "M8x30", "label M8x30");
    return failures ? 1 : 0;
}

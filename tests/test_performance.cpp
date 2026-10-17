// How long the things that should feel quick take on bigger objects: one step of a drag,
// changing a part of a joined object, a full rebuild. Prints the times and fails only
// well above what they take now, so a change that makes them much slower shows up.
#include "document/document.hpp"
#include "document/recipe.hpp"
#include "document/object_parts.hpp"
#include "document/group/group.hpp"
#include "nlohmann/json.hpp"
#include "preferences/preferences.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <vector>

using namespace dune3d;

// the core reads a few defaults from the preferences; tests use the built-in ones
const Preferences &Preferences::get()
{
    static Preferences the_preferences;
    return the_preferences;
}

static int failures = 0;

// the middle of a few runs, in ms
static double time_ms(const std::function<void()> &f, int runs = 5)
{
    std::vector<double> t;
    for (int i = 0; i < runs; i++) {
        const auto t0 = std::chrono::steady_clock::now();
        f();
        t.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    std::ranges::sort(t);
    return t.at(t.size() / 2);
}

static void report(const std::string &what, double ms, double limit)
{
    const bool ok = ms <= limit;
    std::cout << (ok ? "  ok   " : "  SLOW ") << what << ": " << ms << " ms (limit " << limit << ")" << std::endl;
    if (!ok)
        failures++;
}

static const char *bowl = R"({"id": "bowl", "name": "Bowl", "shape": "revolve", "smooth": true,
    "profile": [[0, 0], [22, 0], [30, 6], [38, 18], [42, 30], [0, 30]],
    "hollow": {"thickness": 2.5, "open": "top"}, "round_edges": 1})";

static const Group *find_group(const Document &doc, const std::string &name, int nth = 0)
{
    for (auto gr : doc.get_groups_sorted()) {
        if (gr->m_name == name && nth-- == 0)
            return gr;
    }
    return nullptr;
}

// one step of dragging the bowl's profile: solve, solids that fit in the time budget
static void drag_step(Document &doc, const Group &sketch)
{
    doc.set_group_solve_pending(sketch.m_uuid);
    doc.update_pending(doc.get_groups_sorted().back()->m_uuid, {}, true);
}

static void test_bowl()
{
    std::cout << "a hollow, rounded bowl" << std::endl;
    Document doc;
    build_recipe(doc, doc.get_groups_sorted().back()->m_uuid, json::parse(std::string("{\"objects\": [") + bowl + "]}"));
    doc.update_pending();
    auto sketch = find_group(doc, "Sketch 2");
    drag_step(doc, *sketch); // learns how long each solid takes
    report("one drag step", time_ms([&] { drag_step(doc, *sketch); }), 150);
    report("everything rebuilt after the drag", time_ms([&] {
               drag_step(doc, *sketch);
               doc.update_pending();
           }),
           1500);
}

static void test_many_bowls()
{
    std::cout << "twelve bowls" << std::endl;
    Document doc;
    json objects = json::array();
    for (int i = 0; i < 12; i++) {
        auto o = json::parse(bowl);
        o["id"] = "bowl" + std::to_string(i);
        o["position"] = {(i % 4) * 100, (i / 4) * 100, 0};
        objects.push_back(o);
    }
    build_recipe(doc, doc.get_groups_sorted().back()->m_uuid, json{{"objects", objects}});
    doc.update_pending();
    // the first bowl's profile: everything after it would be built again
    auto sketch = find_group(doc, "Sketch 2");
    drag_step(doc, *sketch);
    report("one drag step on the first of them", time_ms([&] { drag_step(doc, *sketch); }), 300);
}

static void test_table()
{
    std::cout << "a table of five rounded parts" << std::endl;
    Document doc;
    json objects = {{{"id", "top"}, {"name", "Top"}, {"shape", "box"}, {"width", 100}, {"depth", 60}, {"height", 5},
                     {"position", {0, 0, 70}}, {"round_edges", 1}}};
    const double xs[] = {-42, 42, -42, 42}, ys[] = {-24, -24, 24, 24};
    json combine = json::array();
    for (int i = 0; i < 4; i++) {
        objects.push_back({{"id", "leg" + std::to_string(i)}, {"name", "Leg " + std::to_string(i + 1)},
                           {"shape", "cylinder"}, {"diameter", 6}, {"height", 70}, {"position", {xs[i], ys[i], 0}},
                           {"round_edges", 1}});
        combine.push_back({{"op", "join"}, {"target", "top"}, {"tool", "leg" + std::to_string(i)}});
    }
    auto result =
            build_recipe(doc, doc.get_groups_sorted().back()->m_uuid, json{{"objects", objects}, {"combine", combine}});
    doc.update_pending();
    const auto table = result.bodies.back();
    const auto parts = get_object_parts(doc, table);
    std::vector<UUID> legs;
    for (const auto &set : group_alike_parts(doc, parts)) {
        if (set.parts.size() == 4)
            legs = set.parts;
    }
    if (legs.empty()) {
        std::cout << "  FAIL no legs" << std::endl;
        failures++;
        return;
    }
    double height = 70;
    report("legs' height changed from the panel", time_ms([&] {
               const auto follow = begin_parts_follow(doc, parts, legs);
               size_t i = 0;
               while (i < follow.dims.size() && follow.dims.at(i).label != "Height")
                   i++;
               height = height == 70 ? 80 : 70;
               set_parts_dimension(doc, follow, i, height);
               doc.update_pending();
           }),
           1000);
    report("the whole table moved", time_ms([&] {
               for (const auto &p : parts) {
                   if (auto pos = get_part_position(doc, p))
                       set_part_position(doc, *pos, {5, 0, 0});
               }
               doc.update_pending();
           }),
           1000);
}

int main()
{
    test_bowl();
    test_many_bowls();
    test_table();
    std::cout << (failures ? "FAILED" : "PASSED") << " (" << failures << " too slow)" << std::endl;
    return failures ? 1 : 0;
}

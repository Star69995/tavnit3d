#include "editor.hpp"
#include "workspace_browser.hpp"
#include "document/document.hpp"
#include "document/recipe.hpp"
#include "document/primitives.hpp"
#include "document/body_solids.hpp"
#include "nlohmann/json.hpp"
#include "describe_window.hpp"
#include "dune3d_appwindow.hpp"
#include "dune3d_application.hpp"
#include <array>
#include <cmath>

namespace dune3d {

bool Editor::add_from_recipe(const json &recipe, const std::string &what)
{
    if (m_core.tool_is_active() || !m_core.has_documents())
        return false;
    CanvasUpdater canvas_updater{*this};
    auto &doc = m_core.get_current_document();
    const auto after = doc.get_groups_sorted().back()->m_uuid;
    // next to what's there; the recipe's own coordinates are relative to that spot
    const bool first = get_body_solids(doc, after).empty();
    const auto spot = first ? glm::dvec2(0, 0) : find_free_spot(doc, after, 60);
    RecipeResult result;
    try {
        result = build_recipe(doc, after, recipe, spot);
    }
    catch (const std::exception &e) {
        m_workspace_browser->show_toast("Couldn't make it: " + std::string(e.what()));
        return false;
    }
    if (result.bodies.empty()) {
        m_workspace_browser->show_toast("Nothing came out of that");
        return false;
    }
    const auto doc_uu = m_core.get_current_idocument_info().get_uuid();
    for (const auto &b : result.hidden)
        get_current_document_views()[doc_uu].m_body_views[b].m_visible = false;
    const auto last = doc.get_groups_sorted().back()->m_uuid;
    get_current_document_view().m_current_group = last;
    m_core.set_current_group(last);
    m_core.set_needs_save();
    m_core.rebuild(what);
    m_workspace_browser->update_documents(get_current_document_views());
    set_current_group(last);
    const auto shown = result.bodies.back();
    if (!m_workspace_browser->select_body(doc_uu, shown))
        show_object_editor(doc_uu, shown);
    zoom_to_fit(!first);
    if (result.warnings.size()) {
        std::string w;
        for (const auto &s : result.warnings)
            w += (w.size() ? "; " : "") + s;
        m_workspace_browser->show_toast("Some of it didn't work: " + w);
    }
    return true;
}

void Editor::show_describe_window(const std::string &text)
{
    if (!m_core.has_documents())
        return;
    if (!m_describe_window) {
        m_describe_window = std::make_unique<DescribeWindow>(
                m_win, m_win.get_app().m_user_config.claude_api_key,
                [this](const json &recipe, const std::string &description) {
                    auto what = description.substr(0, 40);
                    return add_from_recipe(recipe, "make: " + what);
                });
    }
    if (text.size())
        m_describe_window->set_description(text);
    m_describe_window->present();
}

// ready-made shapes that are recipes too
void Editor::add_bowl()
{
    add_from_recipe(json::parse(R"({"objects": [{"id": "bowl", "name": "Bowl", "shape": "revolve", "smooth": true,
        "profile": [[0, 0], [22, 0], [30, 6], [38, 18], [42, 30], [0, 30]],
        "hollow": {"thickness": 2.5, "open": "top"}, "round_edges": 1}]})"),
                    "add bowl");
}

namespace {
// a recipe for one object extruded from an outline around the origin
json extruded_recipe(const std::string &name, const std::vector<std::array<double, 2>> &outline, double height,
                     bool smooth, double round_edges = 0)
{
    json o = {{"id", "shape"}, {"name", name}, {"shape", "extrude"}, {"outline", outline}, {"height", height},
              {"smooth", smooth}};
    if (round_edges > 0)
        o["round_edges"] = round_edges;
    return {{"objects", json::array({o})}};
}
} // namespace

void Editor::add_hexagon()
{
    std::vector<std::array<double, 2>> pts;
    for (int i = 0; i < 6; i++)
        pts.push_back({15 * std::cos(i * M_PI / 3), 15 * std::sin(i * M_PI / 3)});
    add_from_recipe(extruded_recipe("Hexagon", pts, 12, false), "add hexagon");
}

void Editor::add_triangle()
{
    // an equilateral triangle, 24 mm a side, as a prism lying on a side face: a roof
    add_from_recipe(extruded_recipe("Triangle", {{-12, 0}, {12, 0}, {0, 20.8}}, 20, false), "add triangle");
}

void Editor::add_star()
{
    std::vector<std::array<double, 2>> pts;
    for (int i = 0; i < 10; i++) {
        const double r = i % 2 ? 8 : 20, a = M_PI / 2 + i * M_PI / 5;
        pts.push_back({r * std::cos(a), r * std::sin(a)});
    }
    add_from_recipe(extruded_recipe("Star", pts, 6, false), "add star");
}

void Editor::add_heart()
{
    std::vector<std::array<double, 2>> pts;
    for (int i = 0; i < 20; i++) {
        const double t = i * 2 * M_PI / 20;
        const double x = 16 * std::pow(std::sin(t), 3);
        const double y = 13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t);
        pts.push_back({std::round(x * 0.8 * 10) / 10, std::round(y * 0.8 * 10) / 10});
    }
    add_from_recipe(extruded_recipe("Heart", pts, 6, true), "add heart");
}

void Editor::add_half_sphere()
{
    // a quarter circle of radius 20 from the rim up to the top, revolved
    json profile = json::array({json::array({0, 0})});
    for (int i = 0; i <= 5; i++) {
        const double a = i * M_PI / 10;
        profile.push_back({std::round(20 * std::cos(a) * 100) / 100, std::round(20 * std::sin(a) * 100) / 100});
    }
    json o = {{"id", "dome"}, {"name", "Half sphere"}, {"shape", "revolve"}, {"smooth", true}, {"profile", profile}};
    add_from_recipe({{"objects", json::array({o})}}, "add half sphere");
}

void Editor::add_vase()
{
    add_from_recipe(json::parse(R"({"objects": [{"id": "vase", "name": "Vase", "shape": "revolve", "smooth": true,
        "profile": [[0, 0], [22, 0], [30, 25], [26, 55], [14, 80], [18, 95], [0, 95]],
        "hollow": {"thickness": 2.5, "open": "top"}, "round_edges": 1}]})"),
                    "add vase");
}

} // namespace dune3d

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

// An M8 bolt: hexagon head, and a shank whose ridges are rings rather than a
// real thread (a helix is slow and often fails in the solid kernel)
void Editor::add_screw()
{
    json shank = json::array({json::array({0, 4}), json::array({3.4, 4})});
    const double pitch = 1.5;
    const int ridges = 12;
    for (int i = 0; i <= 2 * ridges; i++)
        shank.push_back({i % 2 ? 4.0 : 3.4, 5 + i * pitch / 2});
    shank.push_back({0, 5 + ridges * pitch});
    json head_outline = json::array();
    for (int i = 0; i < 6; i++)
        head_outline.push_back({std::round(7.5 * std::cos(i * M_PI / 3) * 100) / 100,
                                std::round(7.5 * std::sin(i * M_PI / 3) * 100) / 100});
    json head = {{"id", "head"}, {"name", "Screw"}, {"shape", "extrude"}, {"outline", head_outline},
                 {"height", 5.3}, {"material", "metal"}, {"color", "#b4b8bc"}};
    json shaft = {{"id", "shank"}, {"shape", "revolve"}, {"profile", shank}};
    json recipe = {{"objects", json::array({head, shaft})},
                   {"combine", json::array({json{{"target", "head"}, {"tool", "shank"}, {"op", "join"}}})}};
    add_from_recipe(recipe, "add screw");
}

void Editor::add_vase()
{
    add_from_recipe(json::parse(R"({"objects": [{"id": "vase", "name": "Vase", "shape": "revolve", "smooth": true,
        "profile": [[0, 0], [22, 0], [30, 25], [26, 55], [14, 80], [18, 95], [0, 95]],
        "hollow": {"thickness": 2.5, "open": "top"}, "round_edges": 1}]})"),
                    "add vase");
}

} // namespace dune3d

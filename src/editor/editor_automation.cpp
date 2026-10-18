#include "editor.hpp"
#include "dune3d_appwindow.hpp"
#include "canvas/canvas.hpp"
#include "util/fs_util.hpp"
#include "document/document.hpp"
#include "document/object_parts.hpp"
#include "document/screw.hpp"
#include "nlohmann/json.hpp"
#include "action_search.hpp"
#include "document/group/igroup_solid_model.hpp"
#include "workspace_browser.hpp"
#include "action/action_catalog.hpp"
#include "core/tool_id.hpp"
#include "logger/logger.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include "core/tool.hpp"
#include <fstream>
#include <map>
#include <iostream>
#include <sstream>

namespace dune3d {

static std::map<std::string, glm::dvec3> s_light_marks; // "light-mark": where a light was

/*
 * UI automation for development and visual tests, enabled by pointing the
 * DUNE3D_AUTOMATION environment variable at a script. Everything happens
 * inside the app, so the real mouse and keyboard are never touched and the
 * window needn't be in front. One command per line, '#' starts a comment:
 *
 *   wait MS                  pause
 *   action NAME              trigger an action or tool by its catalog name (ADD_BOX, DRAW_CIRCLE_2D, ...)
 *   open PATH                open a document
 *   move X Y / click X Y     pointer on the 3D view, in window pixels (as in a shot)
 *   drag X1 Y1 X2 Y2         left-button drag on the 3D view (moves an object grabbed at X1 Y1)
 *   rotate X1 Y1 X2 Y2       orbit the view as if dragging between the points
 *   zoom F [X Y]             zoom in by F (out if F < 1), towards window pixel X Y if given
 *   button LABEL             click the visible, enabled button whose text is LABEL
 *   field LABEL VALUE        type VALUE into the input next to the panel label LABEL (any window);
 *                            LABEL=VALUE if the value has spaces
 *   tree NAME [N]            select the N-th (from 0) row called NAME in the workspace browser
 *   escape / commit          cancel / confirm the active tool
 *   winaction NAME [PARAM]   activate a window action, e.g. "winaction grid_step 5"
 *   size W H                 resize the window
 *   shot PATH                save the window contents as PNG
 *   shotwin TITLE PATH       the same for another window, e.g. a dialog
 *   picture PATH             add a picture as a flat object
 *   pattern N [C]            like "sticker", with the N-th pattern (0 stripes, 1 dots, 2 checker, 3 grid) in colour C
 *   sticker PATH             add a sticker with this picture to the object in the panel (then click on it)
 *   save PATH                save the current document under this name
 *   render-window            open the render window (then "button Render", "shotwin Render picture PATH")
 *   layout PATH              write where the picked object and the panel's number fields are
 *   lights                   print where the lights (and what they're aimed at) are on screen and in mm, with colour and brightness
 *   drag-light NAME lamp|target X Y   drag that light's lamp or aimed-at point to window pixel X Y
 *   light-mark NAME lamp|target       remember where it is; then "expect-light-moved|expect-light-same NAME lamp|target"
 *   expect-light NAME lamp|target X Y Z   fail unless it is there (mm, within 0.5)
 *   expect-lights N          fail unless the document has N lights
 *   handles                  print where the picked object's resize handles are, in window pixels
 *   hide NAME / show NAME    the eye of the object called NAME in the list
 *   handles-count N          fail unless the picked object has N resize handles (0 when it is hidden)
 *   check-solids             fail if a step that makes a solid has none ("not done")
 *   groups                   print the current document's groups, their solids and messages
 *   find TEXT                open Find with this typed in (then "shotwin Find PATH", "key Return")
 *   find-run                 run what Find shows first
 *   describe TEXT            open "Make from a description" with this text (then "button Make it")
 *   screw HEAD D L [plain]   add a screw: HEAD 0 hexagon, 1 socket, 2 pan, 3 countersunk; diameter, length in mm
 *   recipe PATH              build the objects of a recipe (JSON file, see document/recipe.hpp)
 *   import PATH              import objects from a model file
 *   export-gltf PATH         export the shown objects as glTF, from the current view
 *   update-check             check for updates as from the menu (feed: DUNE3D_UPDATE_FEED)
 *   panel-scroll F           scroll the side panel to F (0 top ... 1 bottom)
 *   drag-panel NAME X Y      drag the "palette" or "layers" panel by its grip to window pixel X Y
 *   crash                    crash on purpose, to see the crash report
 *   close TITLE              close the window with this title as with its X button
 *   windows                  print the visible windows and their texts (dialogs aren't in shots)
 *   quit                     close the app
 */

namespace {
std::string widget_text(Gtk::Widget &w)
{
    if (auto la = dynamic_cast<Gtk::Label *>(&w))
        return la->get_text();
    std::string s;
    for (auto ch = w.get_first_child(); ch; ch = ch->get_next_sibling()) {
        s = widget_text(*ch);
        if (s.size())
            return s;
    }
    return s;
}

template <typename T, typename F> T *find_widget(Gtk::Widget &root, F pred)
{
    if (!root.get_visible())
        return nullptr;
    if (auto t = dynamic_cast<T *>(&root); t && pred(*t))
        return t;
    for (auto ch = root.get_first_child(); ch; ch = ch->get_next_sibling()) {
        if (auto r = find_widget<T>(*ch, pred))
            return r;
    }
    return nullptr;
}
} // namespace

void Editor::run_automation(const std::filesystem::path &script)
{
    auto lines = std::make_shared<std::vector<std::string>>();
    {
        std::ifstream ifs(script);
        std::string line;
        while (std::getline(ifs, line)) {
            if (auto i = line.find('#'); i != std::string::npos)
                line.erase(i);
            while (line.size() && std::isspace((unsigned char)line.back()))
                line.pop_back();
            if (line.size())
                lines->push_back(line);
        }
    }
    auto index = std::make_shared<size_t>(0);
    auto step = std::make_shared<std::function<void()>>();
    *step = [this, lines, index, step] {
        if (*index >= lines->size())
            return;
        const auto line = lines->at((*index)++);
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        std::string rest;
        std::getline(iss >> std::ws, rest);
        std::cerr << "automation: " << line << std::endl;
        unsigned int delay = 150;

        auto to_canvas = [this](double x, double y) {
            graphene_point_t in{(float)x, (float)y}, out;
            if (!gtk_widget_compute_point(GTK_WIDGET(m_win.gobj()), GTK_WIDGET(get_canvas().gobj()), &in, &out))
                out = in;
            return glm::vec2(out.x, out.y);
        };
        // left-button drag on the 3D view from a to b (canvas pixels)
        auto drag_on_canvas = [this](glm::vec2 a, glm::vec2 b) {
            get_canvas().inject_motion(a.x, a.y);
            Glib::signal_timeout().connect_once(
                    [this, a, b] {
                        handle_click(1, 1);
                        get_canvas().inject_press(a.x, a.y);
                        for (int i = 1; i <= 10; i++) {
                            const auto p = a + (b - a) * (i / 10.f);
                            get_canvas().inject_motion(p.x, p.y);
                        }
                        get_canvas().inject_release();
                        end_object_drag();
                        end_handle_drag();
                        end_light_drag();
                    },
                    200);
        };
        // "NAME lamp|target ..." -> the light and which of its two points; the rest of the words are left in `more`
        auto find_light_arg = [this](const std::string &args, bool &target, std::string &more) -> const Light & {
            std::istringstream is(args);
            std::string name, which;
            is >> name >> which;
            std::getline(is >> std::ws, more);
            if (which != "lamp" && which != "target")
                throw std::runtime_error("expected NAME lamp|target, got: " + args);
            target = which == "target";
            for (const auto &l : m_core.get_current_document().m_lights)
                if (l.m_name == name)
                    return l;
            throw std::runtime_error("no light " + name);
        };
        auto light_in_window = [this](const Light &l, bool target) {
            const auto p = get_canvas().project_to_window(target ? l.m_target : l.m_position);
            graphene_point_t in{(float)p.x, (float)p.y}, out;
            if (!gtk_widget_compute_point(GTK_WIDGET(get_canvas().gobj()), GTK_WIDGET(m_win.gobj()), &in, &out))
                out = in;
            return glm::vec2(out.x, out.y);
        };
        auto in_tool = [this](InToolActionID action) {
            if (!m_core.tool_is_active())
                return;
            ToolArgs args;
            args.type = ToolEventType::ACTION;
            args.action = action;
            auto r = m_core.tool_update(args);
            tool_process(r);
        };

        try {
            if (cmd == "wait") {
                delay = std::stoi(rest);
            }
            else if (cmd == "action") {
                if (auto a = action_lut.lookup_opt(rest))
                    trigger_action(*a);
                else
                    trigger_action(tool_lut.lookup(rest));
                delay = 500;
            }
            else if (cmd == "open") {
                open_file(rest);
                delay = 2000;
            }
            else if (cmd == "move" || cmd == "click") {
                double x, y;
                std::istringstream(rest) >> x >> y;
                const auto p = to_canvas(x, y);
                get_canvas().inject_motion(p.x, p.y);
                if (cmd == "click") {
                    // let a frame render so the hover pick sees the new pointer position
                    Glib::signal_timeout().connect_once(
                            [this, p] {
                                handle_click(1, 1);
                                get_canvas().inject_click(p.x, p.y);
                                // the button's let go of, as a real click's release does
                                end_object_drag();
                                end_handle_drag();
                                end_light_drag();
                                if (m_core.tool_is_active()) {
                                    ToolArgs args;
                                    args.type = ToolEventType::ACTION;
                                    args.action = InToolActionID::LMB;
                                    auto r = m_core.tool_update(args);
            tool_process(r);
                                }
                            },
                            200);
                    delay = 700;
                }
            }
            else if (cmd == "drag") {
                // left-button drag on the 3D view, e.g. to move an object
                double x1, y1, x2, y2;
                std::istringstream(rest) >> x1 >> y1 >> x2 >> y2;
                drag_on_canvas(to_canvas(x1, y1), to_canvas(x2, y2));
                delay = 900;
            }
            else if (cmd == "drag-light") {
                // drag a light's lamp or its aimed-at point from where it is now to window pixel X Y
                bool target;
                std::string more;
                const auto &l = find_light_arg(rest, target, more);
                double x, y;
                std::istringstream(more) >> x >> y;
                const auto from = light_in_window(l, target);
                drag_on_canvas(to_canvas(from.x, from.y), to_canvas(x, y));
                delay = 900;
            }
            else if (cmd == "light-mark") {
                bool target;
                std::string more;
                const auto &l = find_light_arg(rest, target, more);
                s_light_marks[rest] = target ? l.m_target : l.m_position;
            }
            else if (cmd == "expect-light-moved" || cmd == "expect-light-same") {
                // against the position "light-mark" saw (mm)
                bool target;
                std::string more;
                const auto &l = find_light_arg(rest, target, more);
                const auto it = s_light_marks.find(rest);
                if (it == s_light_marks.end())
                    throw std::runtime_error("no light-mark for " + rest);
                const bool moved = glm::distance(it->second, target ? l.m_target : l.m_position) > 0.5;
                if (moved != (cmd == "expect-light-moved"))
                    throw std::runtime_error(rest + (moved ? " moved" : " did not move"));
            }
            else if (cmd == "expect-light") {
                // NAME lamp|target X Y Z (mm, within 0.5)
                bool target;
                std::string more;
                const auto &l = find_light_arg(rest, target, more);
                glm::dvec3 want;
                std::istringstream(more) >> want.x >> want.y >> want.z;
                const auto &have = target ? l.m_target : l.m_position;
                if (glm::distance(want, have) > 0.5)
                    throw std::runtime_error("light " + rest + ", it is at " + std::to_string(have.x) + " "
                                             + std::to_string(have.y) + " " + std::to_string(have.z));
            }
            else if (cmd == "expect-lights") {
                const auto n = m_core.get_current_document().m_lights.size();
                if (n != std::stoul(rest))
                    throw std::runtime_error("expected " + rest + " lights, there are " + std::to_string(n));
            }
            else if (cmd == "save") {
                m_core.save_as(path_from_string(rest));
            }
            else if (cmd == "render-window") {
                show_render_window();
                delay = 800;
            }
            else if (cmd == "layout") {
                // where things are, in window pixels, for testing with the real mouse
                // (tools/stress-panel.ps1): the picked object's middle, and each number field in
                // the side panel that can be seen: LABEL, its text, its - and its + button
                std::ofstream ofs(path_from_string(rest));
                auto in_window = [this](Gtk::Widget &w, double x, double y) {
                    graphene_point_t in{(float)x, (float)y}, out;
                    if (!gtk_widget_compute_point(GTK_WIDGET(w.gobj()), GTK_WIDGET(m_win.gobj()), &in, &out))
                        out = in;
                    return glm::ivec2(out.x, out.y);
                };
                auto &doc = m_core.get_current_document();
                if (m_object_editor_body && doc.get_groups().contains(m_object_editor_body)) {
                    if (auto b = get_body_bounds(doc, m_object_editor_body)) {
                        const auto p = get_canvas().project_to_window((b->lo + b->hi) / 2.);
                        const auto w = in_window(get_canvas(), p.x, p.y);
                        ofs << "object\t" << w.x << "\t" << w.y << "\n";
                    }
                }
                Gtk::ScrolledWindow *sc = nullptr;
                for (Gtk::Widget *w = m_group_editor_box; w && !sc; w = w->get_parent())
                    sc = dynamic_cast<Gtk::ScrolledWindow *>(w);
                auto grid = dynamic_cast<Gtk::Grid *>(m_object_editor);
                for (auto ch = grid ? grid->get_first_child() : nullptr; ch; ch = ch->get_next_sibling()) {
                    auto sp = dynamic_cast<Gtk::SpinButton *>(ch);
                    if (!sp || !sp->get_mapped())
                        continue;
                    // only what can be seen without scrolling
                    if (sc) {
                        graphene_point_t in{0, (float)sp->get_height() / 2}, out;
                        if (!gtk_widget_compute_point(GTK_WIDGET(sp->gobj()), GTK_WIDGET(sc->gobj()), &in, &out)
                            || out.y < 0 || out.y > sc->get_height())
                            continue;
                    }
                    int col, row, cw, rh;
                    gtk_grid_query_child(grid->gobj(), GTK_WIDGET(sp->gobj()), &col, &row, &cw, &rh);
                    auto la = dynamic_cast<Gtk::Label *>(grid->get_child_at(col - 1, row));
                    const auto text = in_window(*sp, sp->get_width() * .25, sp->get_height() / 2.);
                    std::vector<glm::ivec2> buttons; // - then +
                    for (auto b = sp->get_first_child(); b; b = b->get_next_sibling()) {
                        if (dynamic_cast<Gtk::Button *>(b))
                            buttons.push_back(in_window(*b, b->get_width() / 2., b->get_height() / 2.));
                    }
                    if (buttons.size() != 2)
                        continue;
                    ofs << "field\t" << (la ? la->get_text().raw() : std::string("?")) << "\t" << text.x << "\t"
                        << text.y << "\t" << buttons.at(0).x << "\t" << buttons.at(0).y << "\t" << buttons.at(1).x
                        << "\t" << buttons.at(1).y << "\n";
                }
            }
            else if (cmd == "handles") {
                for (const auto &h : get_object_handles()) {
                    const auto p = get_canvas().project_to_window(h.pos);
                    graphene_point_t in{(float)p.x, (float)p.y}, out;
                    if (!gtk_widget_compute_point(GTK_WIDGET(get_canvas().gobj()), GTK_WIDGET(m_win.gobj()), &in, &out))
                        out = in;
                    std::cerr << "automation: handle " << h.label << " " << (int)out.x << " " << (int)out.y << std::endl;
                }
            }
            else if (cmd == "handles-count") {
                const auto n = get_object_handles().size();
                if (n != std::stoul(rest))
                    throw std::runtime_error("expected " + rest + " handles, there are " + std::to_string(n));
            }
            else if (cmd == "hide" || cmd == "show") {
                bool found = false;
                for (auto gr : m_core.get_current_document().get_groups_sorted()) {
                    if (gr->m_body.has_value() && gr->m_body->m_name == rest) {
                        on_workspace_browser_body_checked(m_core.get_current_idocument_info().get_uuid(), gr->m_uuid,
                                                          cmd == "show");
                        found = true;
                    }
                }
                if (!found)
                    throw std::runtime_error("no object " + rest);
                delay = 400;
            }
            else if (cmd == "lights") {
                // where each light and its aimed-at point are, in window pixels
                for (const auto &l : m_core.get_current_document().m_lights) {
                    for (const bool target : {false, true}) {
                        if (target && l.m_type == Light::Type::POINT)
                            continue;
                        const auto out = light_in_window(l, target);
                        const auto &v = target ? l.m_target : l.m_position;
                        std::cerr << "automation: light " << l.m_name << (target ? " target " : " ") << (int)out.x << " "
                                  << (int)out.y << " at " << v.x << " " << v.y << " " << v.z << " colour " << l.m_color.r
                                  << " " << l.m_color.g << " " << l.m_color.b << " brightness " << l.m_brightness
                                  << std::endl;
                    }
                }
            }
            else if (cmd == "groups") {
                auto &doc = m_core.get_current_document();
                for (auto gr : doc.get_groups_sorted()) {
                    std::cerr << "automation: group " << gr->m_name << " (" << gr->get_type_name() << ")"
                              << (gr->m_uuid == m_core.get_current_group() ? " current" : "")
                              << (IGroupSolidModel::try_get_solid_model(*gr) ? " solid" : "") << std::endl;
                    for (const auto &m : gr->get_messages())
                        std::cerr << "automation:   " << m.message << std::endl;
                }
            }
            else if (cmd == "check-solids") {
                // fails if a step that makes a solid (extrude, rounding, ...) has none: "not done"
                auto &doc = m_core.get_current_document();
                for (auto gr : doc.get_groups_sorted()) {
                    const auto t = gr->get_type();
                    if (t == Group::Type::SKETCH || t == Group::Type::REFERENCE
                        || !dynamic_cast<const IGroupSolidModel *>(gr))
                        continue;
                    if (!IGroupSolidModel::try_get_solid_model(*gr))
                        throw std::runtime_error("no solid in " + gr->m_name);
                }
            }
            else if (cmd == "find") {
                show_search();
                if (auto w = find_widget<Gtk::SearchEntry>(*m_search_window, [](auto &) { return true; }))
                    w->set_text(rest);
                delay = 600;
            }
            else if (cmd == "find-run") {
                // run the first thing found
                if (auto w = find_widget<Gtk::SearchEntry>(*m_search_window, [](auto &) { return true; }))
                    g_signal_emit_by_name(w->gobj(), "activate");
                delay = 800;
            }
            else if (cmd == "describe") {
                show_describe_window(rest);
                delay = 800;
            }
            else if (cmd == "recipe") {
                std::ifstream ifs(path_from_string(rest));
                add_from_recipe(json::parse(ifs), "recipe");
                delay = 1500;
            }
            else if (cmd == "screw") {
                // screw HEAD D L [plain]: HEAD 0 hexagon, 1 socket, 2 pan, 3 countersunk
                ScrewParams p;
                int head = 0;
                std::string plain;
                std::istringstream(rest) >> head >> p.diameter >> p.length >> plain;
                p.head = static_cast<ScrewHead>(head);
                p.thread = plain != "plain";
                add_screw(p);
                delay = 1500;
            }
            else if (cmd == "import") {
                import_object_file(path_from_string(rest));
                delay = 1500;
            }
            else if (cmd == "export-gltf") {
                export_gltf_now(path_from_string(rest));
            }
            else if (cmd == "sticker") {
                if (!m_object_editor)
                    throw std::runtime_error("no object panel");
                begin_place_decal(m_object_editor_doc, m_object_editor_body, path_from_string(rest));
            }
            else if (cmd == "picture") {
                add_picture_object(path_from_string(rest));
            }
            else if (cmd == "pattern") {
                // pattern INDEX [COLOUR INDEX]: as chosen in the object panel; then click on the object
                if (!m_object_editor)
                    throw std::runtime_error("no object panel");
                int pattern = 0, colour = 0;
                std::istringstream(rest) >> pattern >> colour;
                begin_place_pattern(m_object_editor_doc, m_object_editor_body, pattern, colour);
            }
            else if (cmd == "zoom") {
                // zoom F [X Y]: F > 1 zooms in, towards the window pixel X Y if given
                double f = 1, x = -1, y = -1;
                std::istringstream(rest) >> f >> x >> y;
                if (x >= 0 && y >= 0) {
                    const auto p = to_canvas(x, y);
                    get_canvas().inject_motion(p.x, p.y);
                }
                get_canvas().set_cam_distance(get_canvas().get_cam_distance() / f,
                                              x >= 0 ? Canvas::ZoomCenter::CURSOR : Canvas::ZoomCenter::SCREEN);
                delay = 400;
            }
            else if (cmd == "rotate") {
                double x1, y1, x2, y2;
                std::istringstream(rest) >> x1 >> y1 >> x2 >> y2;
                get_canvas().inject_rotate(to_canvas(x1, y1), to_canvas(x2, y2));
                delay = 400;
            }
            else if (cmd == "button") {
                Gtk::Button *bu = nullptr;
                for (auto w : Gtk::Window::list_toplevels()) {
                    if (!bu && w->get_visible())
                        bu = find_widget<Gtk::Button>(*w, [&rest](Gtk::Button &b) {
                            return b.is_sensitive() && (b.get_label().raw() == rest || widget_text(b) == rest);
                        });
                }
                if (!bu)
                    throw std::runtime_error("no button " + rest);
                bu->activate();
                delay = 600;
            }
            else if (cmd == "field") {
                // "LABEL VALUE" with the value being the last word ("Round edges 2"),
                // or "LABEL=VALUE" when the value has spaces too ("Cell size=5 mm")
                const auto sp = rest.find('=') != std::string::npos ? rest.find('=') : rest.rfind(' ');
                const auto label = rest.substr(0, sp);
                const auto value = rest.substr(sp + 1);
                Gtk::Label *la = nullptr;
                for (auto w : Gtk::Window::list_toplevels()) {
                    if (!la && w->get_visible())
                        la = find_widget<Gtk::Label>(*w, [&label](Gtk::Label &l) {
                            return l.get_text().raw() == label && dynamic_cast<Gtk::Grid *>(l.get_parent());
                        });
                }
                auto grid = la ? dynamic_cast<Gtk::Grid *>(la->get_parent()) : nullptr;
                if (!grid)
                    throw std::runtime_error("no field " + label);
                int col, row, w, h;
                gtk_grid_query_child(grid->gobj(), GTK_WIDGET(la->gobj()), &col, &row, &w, &h);
                auto widget = grid->get_child_at(col + 1, row);
                if (auto box = dynamic_cast<Gtk::Box *>(widget); box && box->get_first_child())
                    widget = box->get_first_child(); // a field with buttons next to it
                if (auto sp = dynamic_cast<Gtk::SpinButton *>(widget)) {
                    sp->set_text(value);
                    sp->update();
                    sp->activate();
                }
                else if (auto en = dynamic_cast<Gtk::Entry *>(widget)) {
                    en->set_text(value);
                    g_signal_emit_by_name(en->gobj(), "activate"); // like pressing Enter
                }
                else if (auto range = dynamic_cast<Gtk::Range *>(widget)) {
                    range->set_value(std::stod(value));
                }
                else if (auto sw = dynamic_cast<Gtk::Switch *>(widget)) {
                    sw->set_active(value == "on" || value == "1" || value == "true");
                }
                else if (auto dd = dynamic_cast<Gtk::DropDown *>(widget)) {
                    auto model = dd->get_model();
                    for (guint i = 0; i < model->get_n_items(); i++) {
                        auto so = std::dynamic_pointer_cast<Gtk::StringObject>(model->get_object(i));
                        if (so && so->get_string().raw() == value)
                            dd->set_selected(i);
                    }
                }
                else {
                    throw std::runtime_error("unsupported field " + label);
                }
                delay = 800;
            }
            else if (cmd == "tree") {
                // tree NAME [N]: the N-th row called NAME, N counting from 0
                std::string name = rest;
                unsigned int nth = 0;
                if (auto sp = rest.rfind(' '); sp != std::string::npos && std::isdigit((unsigned char)rest[sp + 1])) {
                    name = rest.substr(0, sp);
                    nth = std::stoi(rest.substr(sp + 1));
                }
                if (!m_workspace_browser->select_row_by_name(name, nth))
                    throw std::runtime_error("no tree row " + name);
                delay = 600;
            }
            else if (cmd == "escape") {
                in_tool(InToolActionID::CANCEL);
            }
            else if (cmd == "commit") {
                in_tool(InToolActionID::RMB);
            }
            else if (cmd == "winaction") {
                const auto sp = rest.find(' ');
                auto act = m_win.lookup_action(rest.substr(0, sp));
                if (!act)
                    throw std::runtime_error("no window action " + rest);
                if (sp == std::string::npos)
                    act->activate();
                else
                    act->activate_variant(Glib::Variant<Glib::ustring>::create(rest.substr(sp + 1)));
                delay = 500;
            }
            else if (cmd == "size") {
                int w, h;
                std::istringstream(rest) >> w >> h;
                m_win.set_default_size(w, h);
                delay = 800;
            }
            else if (cmd == "shot" || cmd == "shotwin") {
                // shot PATH: the main window; shotwin TITLE... PATH: another window by its title
                Gtk::Window *win = &m_win;
                std::string path = rest;
                if (cmd == "shotwin") {
                    const auto sp = rest.rfind(' ');
                    const auto title = rest.substr(0, sp);
                    path = rest.substr(sp + 1);
                    win = nullptr;
                    for (auto w : Gtk::Window::list_toplevels()) {
                        if (w->get_visible() && w->get_title().raw() == title)
                            win = w;
                    }
                    if (!win)
                        throw std::runtime_error("no window " + title);
                }
                const int w = win->get_width(), h = win->get_height();
                auto paintable = gtk_widget_paintable_new(GTK_WIDGET(win->gobj()));
                auto snapshot = gtk_snapshot_new();
                gdk_paintable_snapshot(paintable, snapshot, w, h);
                auto node = gtk_snapshot_free_to_node(snapshot);
                if (node) {
                    auto renderer = gtk_native_get_renderer(GTK_NATIVE(win->gobj()));
                    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, (float)w, (float)h);
                    auto texture = gsk_renderer_render_texture(renderer, node, &bounds);
                    gdk_texture_save_to_png(texture, path.c_str());
                    g_object_unref(texture);
                    gsk_render_node_unref(node);
                }
                g_object_unref(paintable);
            }
            else if (cmd == "update-check") {
                check_for_updates(true);
                delay = 2500;
            }
            else if (cmd == "windows") {
                // print every toplevel and the texts in it, e.g. to see what a dialog says
                std::function<void(Gtk::Widget &, std::string &)> collect = [&collect](Gtk::Widget &w,
                                                                                      std::string &out) {
                    if (auto la = dynamic_cast<Gtk::Label *>(&w); la && la->get_text().size())
                        out += " | " + la->get_text();
                    for (auto ch = w.get_first_child(); ch; ch = ch->get_next_sibling())
                        collect(*ch, out);
                };
                for (auto win : Gtk::Window::list_toplevels()) {
                    if (!win->get_visible())
                        continue;
                    std::string texts;
                    collect(*win, texts);
                    std::cerr << "automation: window '" << win->get_title() << "' " << G_OBJECT_TYPE_NAME(win->gobj())
                              << texts.substr(0, 600) << std::endl;
                }
            }
            else if (cmd == "panel-scroll") {
                // scroll the side panel (object panel, group editor) to a fraction of its length
                double f = 1;
                std::istringstream(rest) >> f;
                for (Gtk::Widget *w = m_group_editor_box; w; w = w->get_parent()) {
                    if (auto sc = dynamic_cast<Gtk::ScrolledWindow *>(w)) {
                        auto adj = sc->get_vadjustment();
                        adj->set_value(adj->get_lower() + f * (adj->get_upper() - adj->get_page_size() - adj->get_lower()));
                        break;
                    }
                }
            }
            else if (cmd == "drag-panel") {
                std::istringstream is(rest);
                std::string name;
                double x = 0, y = 0;
                is >> name >> x >> y;
                if (!m_win.drag_panel(name, x, y))
                    std::cerr << "automation: no panel " << name << std::endl;
            }
            else if (cmd == "crash") {
                // to see the crash report (util/crash_report.cpp)
                volatile int *nothing = nullptr;
                *nothing = 1;
            }
            else if (cmd == "close") {
                // close a window as with its X button
                for (auto w : Gtk::Window::list_toplevels()) {
                    if (w->get_visible() && w->get_title().raw() == rest)
                        w->close();
                }
            }
            else if (cmd == "quit") {
                // no "save changes?" prompt for automated runs
                m_win.get_application()->quit();
                return;
            }
            else {
                throw std::runtime_error("unknown command");
            }
        }
        catch (const std::exception &e) {
            std::cerr << "automation: FAILED " << line << ": " << e.what() << std::endl;
        }
        Glib::signal_timeout().connect_once([step] { (*step)(); }, delay);
    };
    Glib::signal_timeout().connect_once([step] { (*step)(); }, 1000);
}

} // namespace dune3d

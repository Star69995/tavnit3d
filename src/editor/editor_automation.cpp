#include "editor.hpp"
#include "dune3d_appwindow.hpp"
#include "canvas/canvas.hpp"
#include "workspace_browser.hpp"
#include "action/action_catalog.hpp"
#include "core/tool_id.hpp"
#include "logger/logger.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include "core/tool.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

namespace dune3d {

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
 *   button LABEL             click the visible, enabled button whose text is LABEL
 *   field LABEL VALUE        type VALUE into the input next to the panel label LABEL
 *   tree NAME [N]            select the N-th (from 0) row called NAME in the workspace browser
 *   escape / commit          cancel / confirm the active tool
 *   winaction NAME [PARAM]   activate a window action, e.g. "winaction grid_step 5"
 *   size W H                 resize the window
 *   shot PATH                save the window contents as PNG
 *   update-check             check for updates as from the menu (feed: DUNE3D_UPDATE_FEED)
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
                const auto a = to_canvas(x1, y1), b = to_canvas(x2, y2);
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
                        },
                        200);
                delay = 900;
            }
            else if (cmd == "rotate") {
                double x1, y1, x2, y2;
                std::istringstream(rest) >> x1 >> y1 >> x2 >> y2;
                get_canvas().inject_rotate(to_canvas(x1, y1), to_canvas(x2, y2));
                delay = 400;
            }
            else if (cmd == "button") {
                auto bu = find_widget<Gtk::Button>(m_win, [&rest](Gtk::Button &b) {
                    return b.is_sensitive() && (b.get_label() == rest || widget_text(b) == rest);
                });
                if (!bu)
                    throw std::runtime_error("no button " + rest);
                bu->activate();
                delay = 600;
            }
            else if (cmd == "field") {
                // the value is the last word; labels may have spaces ("Round edges 2")
                const auto sp = rest.rfind(' ');
                const auto label = rest.substr(0, sp);
                const auto value = rest.substr(sp + 1);
                auto la = find_widget<Gtk::Label>(m_win, [&label](Gtk::Label &l) { return l.get_text() == label; });
                auto grid = la ? dynamic_cast<Gtk::Grid *>(la->get_parent()) : nullptr;
                if (!grid)
                    throw std::runtime_error("no field " + label);
                int col, row, w, h;
                gtk_grid_query_child(grid->gobj(), GTK_WIDGET(la->gobj()), &col, &row, &w, &h);
                auto widget = grid->get_child_at(col + 1, row);
                if (auto sp = dynamic_cast<Gtk::SpinButton *>(widget)) {
                    sp->set_text(value);
                    sp->update();
                    sp->activate();
                }
                else if (auto en = dynamic_cast<Gtk::Entry *>(widget)) {
                    en->set_text(value);
                    en->activate();
                }
                else if (auto dd = dynamic_cast<Gtk::DropDown *>(widget)) {
                    auto model = dd->get_model();
                    for (guint i = 0; i < model->get_n_items(); i++) {
                        auto so = std::dynamic_pointer_cast<Gtk::StringObject>(model->get_object(i));
                        if (so && so->get_string() == value)
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
            else if (cmd == "shot") {
                const int w = m_win.get_width(), h = m_win.get_height();
                auto paintable = gtk_widget_paintable_new(GTK_WIDGET(m_win.gobj()));
                auto snapshot = gtk_snapshot_new();
                gdk_paintable_snapshot(paintable, snapshot, w, h);
                auto node = gtk_snapshot_free_to_node(snapshot);
                if (node) {
                    auto renderer = gtk_native_get_renderer(GTK_NATIVE(m_win.gobj()));
                    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, (float)w, (float)h);
                    auto texture = gsk_renderer_render_texture(renderer, node, &bounds);
                    gdk_texture_save_to_png(texture, rest.c_str());
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

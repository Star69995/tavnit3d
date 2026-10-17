#include "dune3d_appwindow.hpp"
#include "dune3d_application.hpp"
#include "canvas/canvas.hpp"
#include <algorithm>

namespace dune3d {

/*
 * Panels that can be dragged around by a grip: the tool palette over the 3D view, and the layers
 * list, which sits in the sidebar until its header is dragged out over the view (and goes back
 * when dropped on the sidebar). One drag gesture on the whole window does it, so a drag goes on
 * when the panel changes parent halfway through.
 */

namespace {
graphene_point_t map_point(Gtk::Widget &from, Gtk::Widget &to, double x, double y)
{
    graphene_point_t in{static_cast<float>(x), static_cast<float>(y)};
    graphene_point_t out{0, 0};
    if (!gtk_widget_compute_point(from.gobj(), to.gobj(), &in, &out))
        return in;
    return out;
}

Gtk::Widget &make_grip_bar()
{
    auto bar = Gtk::make_managed<Gtk::Box>();
    bar->add_css_class("panel-grip-bar");
    bar->set_size_request(28, 4);
    bar->set_halign(Gtk::Align::CENTER);
    bar->set_valign(Gtk::Align::CENTER);
    return *bar;
}

constexpr int floating_layers_width = 270;
constexpr int floating_layers_height = 380;
} // namespace

void Dune3DAppWindow::init_movable_panels(Gtk::Box &palette_box)
{
    {
        auto grip = Gtk::make_managed<Gtk::Box>();
        grip->add_css_class("panel-grip");
        grip->set_size_request(-1, 12);
        auto &bar = make_grip_bar();
        bar.set_hexpand(true);
        grip->append(bar);
        grip->set_tooltip_text("Drag to move the palette");
        grip->set_cursor("grab");
        palette_box.prepend(*grip);
        add_movable_panel("palette", *m_action_bar_revealer, *grip);
        if (auto it = m_app.m_user_config.panels.find("palette");
            it != m_app.m_user_config.panels.end() && it->second.x >= 0) {
            m_action_bar_revealer->set_margin_start(it->second.x);
            m_action_bar_revealer->set_margin_top(it->second.y);
        }
    }

    auto gesture = Gtk::GestureDrag::create();
    gesture->set_button(GDK_BUTTON_PRIMARY);
    gesture->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    gesture->signal_drag_begin().connect([this, gesture](double x, double y) {
        auto panel = panel_at(x, y);
        if (!panel) {
            gesture->set_state(Gtk::EventSequenceState::DENIED);
            return;
        }
        gesture->set_state(Gtk::EventSequenceState::CLAIMED);
        panel_drag_begin(*panel, x, y);
    });
    gesture->signal_drag_update().connect([this, gesture](double dx, double dy) {
        double x, y;
        if (m_panel_drag && gesture->get_start_point(x, y))
            panel_drag_update(x + dx, y + dy);
    });
    gesture->signal_drag_end().connect([this, gesture](double dx, double dy) {
        double x, y;
        if (m_panel_drag && gesture->get_start_point(x, y))
            panel_drag_end(x + dx, y + dy);
    });
    add_controller(gesture);

    // a smaller window mustn't leave a panel out of reach
    m_canvas->signal_resize().connect([this](int, int) {
        Glib::signal_idle().connect_once([this] { keep_panels_in_view(); });
    });
}

Dune3DAppWindow::MovablePanel &Dune3DAppWindow::add_movable_panel(const std::string &name, Gtk::Widget &widget,
                                                                   Gtk::Widget &grip)
{
    auto &panel = m_panels.emplace_back();
    panel.name = name;
    panel.widget = &widget;
    panel.grip = &grip;
    return panel;
}

void Dune3DAppWindow::set_layers_panel(Gtk::Widget &content)
{
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    box->add_css_class("layers-panel");

    auto header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    header->add_css_class("layers-panel-header");
    auto grip = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    grip->add_css_class("panel-grip");
    grip->set_hexpand(true);
    grip->set_cursor("grab");
    auto &bar = make_grip_bar();
    bar.set_size_request(4, 16);
    grip->append(bar);
    auto label = Gtk::make_managed<Gtk::Label>("Layers");
    label->add_css_class("action-bar-section");
    label->set_xalign(0);
    grip->append(*label);
    header->append(*grip);
    auto button = Gtk::make_managed<Gtk::Button>();
    button->set_has_frame(false);
    button->add_css_class("layers-panel-button");
    header->append(*button);
    box->append(*header);

    content.set_vexpand(true);
    box->append(content);

    auto &panel = add_movable_panel("layers", *box, *grip);
    panel.dockable = true;
    panel.floating = false;
    panel.on_dock_changed = [&panel, grip, button] {
        button->set_icon_name(panel.floating ? "action-panel-dock-symbolic" : "action-panel-float-symbolic");
        button->set_tooltip_text(panel.floating ? "Put the layers back in the sidebar (or drag them there)"
                                                : "Float the layers over the 3D view (or drag this header)");
        grip->set_tooltip_text(panel.floating ? "Drag to move the layers; drop on the sidebar to put them back"
                                              : "Drag over the 3D view to float the layers there");
    };
    button->signal_clicked().connect([this, &panel] {
        if (!panel.floating) {
            const auto place = m_app.m_user_config.panels[panel.name];
            set_panel_floating(panel, true);
            if (place.x >= 0)
                place_panel(panel, place.x, place.y);
            else // top right, left of the axes cube and view bar
                place_panel(panel, m_canvas_overlay->get_width() - floating_layers_width - 110, 12);
        }
        else {
            set_panel_floating(panel, false);
        }
        auto &place = m_app.m_user_config.panels[panel.name];
        place.floating = panel.floating;
        if (panel.floating) {
            place.x = panel.widget->get_margin_start();
            place.y = panel.widget->get_margin_top();
        }
    });

    m_left_bar->set_start_child(*box);
    panel.on_dock_changed();
    if (auto it = m_app.m_user_config.panels.find("layers");
        it != m_app.m_user_config.panels.end() && it->second.floating) {
        set_panel_floating(panel, true);
        // the view has no size yet; keep_panels_in_view pulls it in once it has
        box->set_margin_start(std::max(it->second.x, 0));
        box->set_margin_top(std::max(it->second.y, 0));
    }
}

Dune3DAppWindow::MovablePanel *Dune3DAppWindow::panel_at(double win_x, double win_y)
{
    auto w = pick(win_x, win_y);
    if (!w)
        return nullptr;
    for (auto &panel : m_panels) {
        if (w == panel.grip || w->is_ancestor(*panel.grip))
            return &panel;
    }
    return nullptr;
}

bool Dune3DAppWindow::over_sidebar(double win_x) const
{
    auto self = const_cast<Dune3DAppWindow *>(this);
    return map_point(*self, *m_canvas_overlay, win_x, 0).x < 0;
}

void Dune3DAppWindow::panel_drag_begin(MovablePanel &panel, double win_x, double win_y)
{
    const auto p = map_point(*this, *panel.widget, win_x, win_y);
    m_panel_drag = PanelDrag{&panel, p.x, p.y, false};
    panel.grip->set_cursor("grabbing");
}

void Dune3DAppWindow::panel_drag_update(double win_x, double win_y)
{
    auto &drag = *m_panel_drag;
    auto &panel = *drag.panel;
    drag.moved = true;
    if (!panel.floating) {
        if (over_sidebar(win_x))
            return; // still in the sidebar: nothing moves until the pointer is out over the view
        set_panel_floating(panel, true);
        drag.grab_x = std::min(drag.grab_x, floating_layers_width / 2.);
    }
    const auto p = map_point(*this, *m_canvas_overlay, win_x, win_y);
    place_panel(panel, p.x - drag.grab_x, p.y - drag.grab_y);
    if (panel.dockable) {
        if (over_sidebar(win_x))
            m_left_bar->add_css_class("dock-target");
        else
            m_left_bar->remove_css_class("dock-target");
    }
}

void Dune3DAppWindow::panel_drag_end(double win_x, double win_y)
{
    const auto drag = *m_panel_drag;
    m_panel_drag.reset();
    auto &panel = *drag.panel;
    panel.grip->set_cursor("grab");
    m_left_bar->remove_css_class("dock-target");
    if (!drag.moved)
        return;
    if (panel.dockable && panel.floating && over_sidebar(win_x))
        set_panel_floating(panel, false);
    auto &place = m_app.m_user_config.panels[panel.name];
    place.floating = panel.floating;
    if (panel.floating) {
        place.x = panel.widget->get_margin_start();
        place.y = panel.widget->get_margin_top();
    }
}

bool Dune3DAppWindow::drag_panel(const std::string &name, double x, double y)
{
    auto it = std::ranges::find_if(m_panels, [&name](auto &p) { return p.name == name; });
    if (it == m_panels.end() || !it->grip->get_mapped())
        return false;
    const auto start = map_point(*it->grip, *this, 8, it->grip->get_height() / 2.);
    panel_drag_begin(*it, start.x, start.y);
    // in steps, as the pointer would go, so leaving the sidebar is noticed on the way
    for (int i = 1; i <= 10; i++)
        panel_drag_update(start.x + (x - start.x) * i / 10, start.y + (y - start.y) * i / 10);
    panel_drag_end(x, y);
    return true;
}

void Dune3DAppWindow::set_panel_floating(MovablePanel &panel, bool floating)
{
    if (panel.floating == floating)
        return;
    auto &w = *panel.widget;
    g_object_ref(w.gobj()); // keep it alive between the two parents
    if (floating) {
        m_left_bar->unset_start_child();
        w.set_halign(Gtk::Align::START);
        w.set_valign(Gtk::Align::START);
        w.set_size_request(floating_layers_width, floating_layers_height);
        w.add_css_class("floating");
        m_canvas_overlay->add_overlay(w);
    }
    else {
        m_canvas_overlay->remove_overlay(w);
        w.set_halign(Gtk::Align::FILL);
        w.set_valign(Gtk::Align::FILL);
        w.set_margin_start(0);
        w.set_margin_top(0);
        w.set_size_request(-1, -1);
        w.remove_css_class("floating");
        m_left_bar->set_start_child(w);
    }
    g_object_unref(w.gobj());
    panel.floating = floating;
    if (panel.on_dock_changed)
        panel.on_dock_changed();
}

void Dune3DAppWindow::place_panel(MovablePanel &panel, double x, double y)
{
    auto &w = *panel.widget;
    // until it's been shown its size isn't known; the floating layers have a fixed one
    int width = w.get_width();
    if (panel.dockable && width == 0)
        width = floating_layers_width;
    // a panel may hang below the view's bottom edge, but its top stays in reach
    const int max_x = std::max(0, m_canvas_overlay->get_width() - width);
    const int max_y = std::max(0, m_canvas_overlay->get_height() - 60);
    w.set_margin_start(std::clamp(static_cast<int>(x), 0, max_x));
    w.set_margin_top(std::clamp(static_cast<int>(y), 0, max_y));
}

void Dune3DAppWindow::keep_panels_in_view()
{
    if (m_panel_drag || m_canvas_overlay->get_width() == 0)
        return;
    for (auto &panel : m_panels) {
        if (panel.floating && panel.widget->get_mapped())
            place_panel(panel, panel.widget->get_margin_start(), panel.widget->get_margin_top());
    }
}

} // namespace dune3d

#include "editor.hpp"
#include "dune3d_appwindow.hpp"
#include "preferences/preferences.hpp"
#include "util/gtk_util.hpp"
#include "util/util.hpp"

namespace dune3d {

/*
 * Everything about the workplane grid in one small window: spacing and snapping
 * (per workspace view, like the View menu items) and its look (colour,
 * opacity, major lines; saved in the preferences). Changes show immediately.
 */
void Editor::show_grid_settings()
{
    if (m_grid_settings_window) {
        m_grid_settings_window->present();
        return;
    }
    auto win = new Gtk::Window;
    m_grid_settings_window = win;
    win->set_title("Grid");
    win->set_transient_for(m_win);
    win->set_hide_on_close(false);
    win->set_resizable(false);
    win->signal_close_request().connect(
            [this, win] {
                m_grid_settings_window = nullptr;
                Glib::signal_idle().connect_once([win] { delete win; });
                return false;
            },
            false);

    auto grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(8);
    grid->set_column_spacing(12);
    grid->set_margin(16);
    int top = 0;

    auto look_changed = [this] {
        m_preferences.signal_changed().emit(); // applies the appearance
        m_preferences.save();
        canvas_update_keep_selection(); // major lines are decided when drawing
    };

    {
        static const std::vector<std::pair<double, const char *>> steps = {
                {0, "Automatic"}, {1, "1 mm"}, {2, "2 mm"}, {5, "5 mm"}, {10, "10 mm"}, {20, "20 mm"}, {50, "50 mm"}};
        auto items = Gtk::StringList::create();
        guint sel = 0;
        const double current = m_core.has_documents() ? get_current_workspace_view().m_grid_step : 0;
        for (guint i = 0; i < steps.size(); i++) {
            items->append(steps.at(i).second);
            if (steps.at(i).first == current)
                sel = i;
        }
        auto dd = Gtk::make_managed<Gtk::DropDown>(items);
        dd->set_selected(sel);
        dd->property_selected().signal_changed().connect(
                [this, dd] { set_grid_step(steps.at(dd->get_selected()).first); });
        grid_attach_label_and_widget(*grid, "Cell size", *dd, top);
    }
    {
        auto sw = Gtk::make_managed<Gtk::Switch>();
        sw->set_halign(Gtk::Align::START);
        sw->set_active(m_core.has_documents() ? get_current_workspace_view().m_snap_to_grid : true);
        sw->property_active().signal_changed().connect([this, sw] { set_snap_to_grid(sw->get_active()); });
        grid_attach_label_and_widget(*grid, "Snap to grid", *sw, top);
    }
    auto &a = m_preferences.canvas.appearance;
    {
        auto dia = Gtk::ColorDialog::create();
        dia->set_with_alpha(false);
        auto bu = Gtk::make_managed<Gtk::ColorDialogButton>(dia);
        bu->set_rgba(rgba_from_color(a.grid_color));
        bu->property_rgba().signal_changed().connect([this, bu, look_changed] {
            m_preferences.canvas.appearance.grid_color = color_from_rgba(bu->get_rgba());
            look_changed();
        });
        grid_attach_label_and_widget(*grid, "Colour", *bu, top);
    }
    auto add_opacity = [&grid, &top, look_changed](const char *label, float &value) {
        auto scale = Gtk::make_managed<Gtk::Scale>(Gtk::Orientation::HORIZONTAL);
        scale->set_range(0, 100);
        scale->set_value(value * 100);
        scale->set_draw_value(true);
        scale->set_value_pos(Gtk::PositionType::RIGHT);
        scale->set_size_request(200, -1);
        scale->signal_value_changed().connect([scale, &value, look_changed] {
            value = scale->get_value() / 100;
            look_changed();
        });
        grid_attach_label_and_widget(*grid, label, *scale, top);
    };
    add_opacity("Opacity %", a.grid_alpha);
    {
        auto sp = Gtk::make_managed<Gtk::SpinButton>();
        sp->set_range(0, 50);
        sp->set_increments(1, 5);
        sp->set_value(a.grid_major_every);
        sp->set_tooltip_text("A stronger line every so many cells, 0 for none");
        sp->signal_value_changed().connect([this, sp, look_changed] {
            m_preferences.canvas.appearance.grid_major_every = sp->get_value_as_int();
            look_changed();
        });
        grid_attach_label_and_widget(*grid, "Major line every", *sp, top);
    }
    add_opacity("Major opacity %", a.grid_major_alpha);
    {
        auto reset = Gtk::make_managed<Gtk::Button>("Defaults");
        reset->set_halign(Gtk::Align::END);
        reset->signal_clicked().connect([this, win] {
            const Appearance defaults;
            auto &ap = m_preferences.canvas.appearance;
            ap.grid_color = defaults.grid_color;
            ap.grid_alpha = defaults.grid_alpha;
            ap.grid_major_alpha = defaults.grid_major_alpha;
            ap.grid_major_every = defaults.grid_major_every;
            m_preferences.signal_changed().emit();
            m_preferences.save();
            canvas_update_keep_selection();
            win->close(); // reopen with fresh values
            Glib::signal_idle().connect_once([this] { show_grid_settings(); });
        });
        grid->attach(*reset, 0, top++, 2, 1);
    }
    win->set_child(*grid);
    win->present();
}

} // namespace dune3d

#include "preferences_window.hpp"
#include "preferences.hpp"
#include "preferences_window_keys.hpp"
#include "preferences_window_in_tool_keys.hpp"
#include "preferences_window_canvas.hpp"
#include "preferences_window_misc.hpp"

namespace dune3d {

PreferencesWindow::PreferencesWindow(Preferences &prefs) : Gtk::Window(), m_preferences(prefs)
{
    set_default_size(900, 680);
    add_css_class("studio-prefs");
    // set_type_hint();
    auto header = Gtk::make_managed<Gtk::HeaderBar>();
    header->set_show_title_buttons(true);
    set_title("Preferences");
    set_titlebar(*header);
    // header->show();

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    auto sidebar = Gtk::make_managed<Gtk::StackSidebar>();
    sidebar->add_css_class("prefs-nav");
    sidebar->set_size_request(200, -1);
    box->append(*sidebar);

    m_stack = Gtk::make_managed<Gtk::Stack>();
    m_stack->set_hhomogeneous(false);
    sidebar->set_stack(*m_stack);
    box->append(*m_stack);

    {
        auto ed = CanvasPreferencesEditor::create(m_preferences);
        m_stack->add(*ed, "canvas", "Appearance and theme");
        ed->unreference();
    }
    {
        auto ed = Gtk::make_managed<MiscPreferencesEditor>(m_preferences);
        m_stack->add(*ed, "editor", "Tools and panels");
    }
    {
        auto ed = KeySequencesPreferencesEditor::create(m_preferences);
        m_stack->add(*ed, "keys", "Keyboard");
        ed->unreference();
    }
    {
        auto ed = InToolKeySequencesPreferencesEditor::create(m_preferences);
        m_stack->add(*ed, "in_tool_keys", "Keys inside tools");
        ed->unreference();
    }

    set_child(*box);
}

void PreferencesWindow::show_page(const std::string &pg)
{
    m_stack->set_visible_child(pg);
}

} // namespace dune3d

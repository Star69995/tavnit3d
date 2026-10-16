#include "action_search.hpp"
#include <algorithm>

namespace dune3d {

static std::string lower(std::string s)
{
    // ASCII only; other scripts (Hebrew) have no case anyway
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return c < 128 ? std::tolower(c) : c; });
    return s;
}

ActionSearchWindow::ActionSearchWindow(Gtk::Window &parent, std::function<std::vector<SearchItem>()> get_items)
    : m_get_items(std::move(get_items))
{
    set_title("Find");
    add_css_class("studio-dialog");
    set_transient_for(parent);
    set_modal(true);
    set_default_size(460, 420);
    set_hide_on_close(true);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(12);
    set_child(*box);

    m_entry = Gtk::make_managed<Gtk::SearchEntry>();
    m_entry->set_placeholder_text("What are you looking for? (e.g. sticker, hollow, light)");
    m_entry->signal_search_changed().connect([this] { update(); });
    m_entry->signal_activate().connect([this] { run(0); });
    box->append(*m_entry);

    auto sc = Gtk::make_managed<Gtk::ScrolledWindow>();
    sc->set_vexpand(true);
    sc->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_list = Gtk::make_managed<Gtk::ListBox>();
    m_list->add_css_class("navigation-sidebar");
    m_list->set_activate_on_single_click(true);
    m_list->signal_row_activated().connect([this](Gtk::ListBoxRow *row) { run(row->get_index()); });
    sc->set_child(*m_list);
    box->append(*sc);

    m_empty = Gtk::make_managed<Gtk::Label>("Nothing by that name - try another word");
    m_empty->add_css_class("dim-label");
    m_empty->set_visible(false);
    box->append(*m_empty);

    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(
            [this](guint keyval, guint, Gdk::ModifierType) {
                if (keyval == GDK_KEY_Escape) {
                    hide();
                    return true;
                }
                if (keyval == GDK_KEY_Down && m_entry->has_focus()) {
                    if (auto row = m_list->get_row_at_index(0)) {
                        m_list->select_row(*row);
                        row->grab_focus();
                    }
                    return true;
                }
                return false;
            },
            false);
    add_controller(keys);
}

void ActionSearchWindow::open(const std::string &text)
{
    m_items = m_get_items();
    // focused while empty: getting the focus selects the text, and the next key typed would
    // replace what was typed so far; then the cursor goes after it, to keep typing
    m_entry->set_text("");
    present();
    m_entry->grab_focus();
    m_entry->set_text(text);
    m_entry->set_position(-1);
    update();
}

void ActionSearchWindow::update()
{
    while (auto row = m_list->get_row_at_index(0))
        m_list->remove(*row);
    m_shown.clear();

    const auto q = lower(m_entry->get_text());
    std::vector<std::pair<int, size_t>> scored;
    for (size_t i = 0; i < m_items.size(); i++) {
        const auto &it = m_items.at(i);
        const auto title = lower(it.title);
        int score = -1;
        if (q.empty())
            score = 0;
        else if (title.starts_with(q))
            score = 3;
        else if (title.find(" " + q) != std::string::npos)
            score = 2;
        else if (title.find(q) != std::string::npos)
            score = 1;
        else if (lower(it.keywords).find(q) != std::string::npos || lower(it.where).find(q) != std::string::npos)
            score = 0;
        if (score >= 0)
            scored.emplace_back(-score, i);
    }
    std::ranges::stable_sort(scored, {}, [](const auto &p) { return p.first; });
    for (const auto &[score, i] : scored) {
        const auto &it = m_items.at(i);
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        row->set_margin(6);
        auto top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto title = Gtk::make_managed<Gtk::Label>(it.title);
        title->set_xalign(0);
        title->set_hexpand(true);
        top->append(*title);
        if (it.keys.size()) {
            auto keys = Gtk::make_managed<Gtk::Label>(it.keys);
            keys->add_css_class("dim-label");
            keys->add_css_class("monospace");
            top->append(*keys);
        }
        row->append(*top);
        auto where = Gtk::make_managed<Gtk::Label>(it.where);
        where->set_xalign(0);
        where->add_css_class("dim-label");
        where->add_css_class("caption");
        row->append(*where);
        m_list->append(*row);
        m_shown.push_back(i);
    }
    m_empty->set_visible(m_shown.empty());
    if (auto row = m_list->get_row_at_index(0))
        m_list->select_row(*row);
}

void ActionSearchWindow::run(size_t row)
{
    if (row >= m_shown.size())
        return;
    auto fn = m_items.at(m_shown.at(row)).run;
    hide();
    // after the window's gone, so the action can open its own windows
    Glib::signal_idle().connect_once([fn] {
        if (fn)
            fn();
    });
}

} // namespace dune3d

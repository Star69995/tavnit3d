#pragma once
#include <gtkmm.h>
#include <functional>
#include <string>
#include <vector>

namespace dune3d {

// One thing the search can find: what it's called, where it is in the
// window (so it can be found again without searching) and what doing it means.
struct SearchItem {
    std::string title;
    std::string where;    // e.g. "Tool palette > Lights"
    std::string keywords; // other words people may type, any language
    std::function<void()> run;
    std::string keys; // keyboard shortcut, if any
};

// "Find": type a few letters of what you're looking for, pick it and it's done.
class ActionSearchWindow : public Gtk::Window {
public:
    ActionSearchWindow(Gtk::Window &parent, std::function<std::vector<SearchItem>()> get_items);
    void open(const std::string &text = {}); // fresh search (with what's typed so far), focused

private:
    std::function<std::vector<SearchItem>()> m_get_items;
    std::vector<SearchItem> m_items;
    std::vector<size_t> m_shown; // indices into m_items, in list order
    Gtk::SearchEntry *m_entry = nullptr;
    Gtk::ListBox *m_list = nullptr;
    Gtk::Label *m_empty = nullptr;

    void update();
    void run(size_t row);
};

} // namespace dune3d

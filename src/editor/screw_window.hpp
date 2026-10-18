#pragma once
#include <gtkmm.h>
#include <functional>
#include "document/screw.hpp"

namespace dune3d {

// "Screw": pick head, size and length, then add it. The last choice is kept
// while the app runs. The window stays around (hidden when closed).
class ScrewWindow : public Gtk::Window {
public:
    using OnAdd = std::function<void(const ScrewParams &)>;
    ScrewWindow(Gtk::Window &parent, OnAdd on_add);

    ScrewParams get_params() const;

private:
    OnAdd m_on_add;
    Gtk::DropDown *m_head = nullptr;
    Gtk::DropDown *m_size = nullptr;
    Glib::RefPtr<Gtk::Adjustment> m_length;
    Gtk::CheckButton *m_thread = nullptr;
    Gtk::Label *m_label = nullptr;
    void update_label();
};

} // namespace dune3d

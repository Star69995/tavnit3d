#include "screw_window.hpp"
#include <array>

namespace dune3d {

static const std::array<double, 7> sizes = {3, 4, 5, 6, 8, 10, 12};

ScrewWindow::ScrewWindow(Gtk::Window &parent, OnAdd on_add) : m_on_add(std::move(on_add))
{
    set_title("Screw");
    add_css_class("studio-dialog");
    set_transient_for(parent);
    set_hide_on_close(true);
    set_default_size(340, -1);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(14);
    set_child(*box);

    auto row = [&](const char *text, Gtk::Widget &w, const char *hint) {
        auto l = Gtk::make_managed<Gtk::Label>(text);
        l->set_xalign(0);
        l->set_tooltip_text(hint);
        w.set_tooltip_text(hint);
        box->append(*l);
        box->append(w);
    };

    m_head = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create(
            {"Hexagon head", "Socket head (cap)", "Pan head (rounded)", "Countersunk head"}));
    row("Head", *m_head, "The shape of the head and how it is turned: hexagon (spanner), socket (Allen key), pan, countersunk");

    Glib::RefPtr<Gtk::StringList> size_names = Gtk::StringList::create({});
    for (const auto s : sizes)
        size_names->append("M" + std::to_string(static_cast<int>(s)));
    m_size = Gtk::make_managed<Gtk::DropDown>(size_names);
    m_size->set_selected(4); // M8
    row("Size (thread diameter)", *m_size, "M8 means a thread 8 mm across; the head size follows");

    m_length = Gtk::Adjustment::create(30, 4, 300, 1, 5);
    auto length = Gtk::make_managed<Gtk::SpinButton>(m_length);
    length->set_numeric(true);
    row("Length under the head (mm)", *length, "How long the part that goes into the material is");

    m_thread = Gtk::make_managed<Gtk::CheckButton>("Thread ridges");
    m_thread->set_active(true);
    m_thread->set_tooltip_text("A real spiral thread; off gives a smooth shank (faster to build)");
    box->append(*m_thread);

    m_label = Gtk::make_managed<Gtk::Label>();
    m_label->add_css_class("dim-label");
    m_label->set_xalign(0);
    box->append(*m_label);

    auto add = Gtk::make_managed<Gtk::Button>("Add screw");
    add->add_css_class("suggested-action");
    add->signal_clicked().connect([this] {
        m_on_add(get_params());
        set_visible(false);
    });
    box->append(*add);

    m_head->property_selected().signal_changed().connect([this] { update_label(); });
    m_size->property_selected().signal_changed().connect([this] { update_label(); });
    m_length->signal_value_changed().connect([this] { update_label(); });
    update_label();
}

ScrewParams ScrewWindow::get_params() const
{
    ScrewParams p;
    p.head = static_cast<ScrewHead>(m_head->get_selected());
    p.diameter = sizes.at(std::min<size_t>(m_size->get_selected(), sizes.size() - 1));
    p.length = m_length->get_value();
    p.thread = m_thread->get_active();
    return p;
}

void ScrewWindow::update_label()
{
    m_label->set_text(screw_label(get_params()));
}

} // namespace dune3d

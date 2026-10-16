#pragma once
#include <gtkmm.h>

namespace dune3d {
class SpinButtonDim : public Gtk::SpinButton {
public:
    SpinButtonDim();
    // "+" before positive values when negative ones are allowed (positions); off for sizes
    void set_show_plus(bool show)
    {
        m_show_plus = show;
    }

protected:
    int on_input(double &new_value);
    bool on_output();
    bool m_show_plus = true;
    // void on_populate_popup(Gtk::Menu *menu) override;
};
} // namespace dune3d

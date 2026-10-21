#pragma once
#include <gtkmm.h>
#include <string>
#include "action/action.hpp"

namespace dune3d {

// hover hints for the object and light panels: every "label | field" row of grid whose label is
// known and that has no hint of its own gets one, on the label and the field
void add_panel_hints(Gtk::Grid &grid);

// a hover hint on each item of a menu (a popover made from a menu model) by its label; the items
// are made when the menu first opens, so this waits for that
void add_menu_hints(Gtk::Popover &popover);

// what a tool palette button does, in a sentence; empty if there's nothing more to say than its name
std::string get_action_hint(const ActionToolID &id);
} // namespace dune3d

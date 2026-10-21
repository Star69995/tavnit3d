#include "panel_hints.hpp"
#include "action/action_id.hpp"
#include "core/tool_id.hpp"
#include <map>

namespace dune3d {

namespace {
const std::map<std::string, std::string> row_hints = {
        // object panel
        {"Name", "The object's name, as shown in the list above; press Enter to change it"},
        {"Colour", "The object's colour; Default goes back to the usual grey"},
        {"Material", "How the surface takes the light: satin, matte, glossy, metal, glass, wood (with grain)..."},
        {"Hole",
         "A hole is drawn see-through and isn't exported; place it inside an object and press Cut to remove it "
         "from what it overlaps"},
        {"Visible", "Show or hide the object; a hidden object stays in the document"},
        {"Says", "The text of the 3D text; press Enter to change it"},
        {"Size", "Height of the letters"},
        {"Width", "Size from side to side (along X)"},
        {"Depth", "Size from front to back (along Y)"},
        {"Height", "Size from bottom to top"},
        {"Diameter", "Distance across the round shape, through its middle"},
        {"Outer diameter", "Distance across the outside of the ring or tube"},
        {"Inner diameter", "Distance across the hole inside"},
        {"Radius", "Distance from the middle to the edge"},
        {"X", "Position left-right (along X), from the middle of the grid"},
        {"Y", "Position front-back (along Y), from the middle of the grid"},
        {"Z", "Position up-down (along Z), from the grid"},
        {"Round edges", "Rounds all edges; 0 for sharp edges"},
        {"Hollow", "Wall thickness: hollows the object out, like a cup; 0 for a solid object"},
        {"Open side", "Which side of a hollow object stays open"},
        {"Sticker", "Width of the picture; the height follows the picture"},
        {"Rotation", "Turns the sticker on the surface, in degrees"},
        {"Pattern", "The pattern and its colour for Add pattern"},
        {"With", "The object to join with, cut out of this one or keep only the overlap of"},
        {"How many", "Number of copies to add"},
        {"Gap", "Space between the copies"},
        // grid settings
        {"Cell size", "Distance between the grid lines; Automatic follows the zoom"},
        {"Snap to grid", "Make points and moves land on grid lines"},
        {"Opacity %", "How strong the grid lines are drawn"},
        {"Major opacity %", "How strong the stronger grid lines are drawn"},
        // light panel
        {"Type", "Lamp: shines all around; Spotlight: a cone of light; Sun: parallel light from far away"},
        {"Brightness", "How bright the light is at the point it shines on"},
        {"Cone angle", "How wide the spotlight's cone of light is"},
};

// "Sticker 2" -> "Sticker"
std::string strip_number(const std::string &s)
{
    const auto sp = s.rfind(' ');
    if (sp == std::string::npos || sp + 1 == s.size())
        return s;
    for (auto i = sp + 1; i < s.size(); i++) {
        if (s.at(i) < '0' || s.at(i) > '9')
            return s;
    }
    return s.substr(0, sp);
}
} // namespace

void add_panel_hints(Gtk::Grid &grid)
{
    for (auto ch = grid.get_first_child(); ch; ch = ch->get_next_sibling()) {
        auto la = dynamic_cast<Gtk::Label *>(ch);
        if (!la || la->get_tooltip_text().size())
            continue;
        int col, row, w, h;
        gtk_grid_query_child(grid.gobj(), ch->gobj(), &col, &row, &w, &h);
        if (col != 0 || w != 1)
            continue; // headings span both columns
        auto it = row_hints.find(la->get_text());
        if (it == row_hints.end())
            it = row_hints.find(strip_number(la->get_text()));
        if (it == row_hints.end())
            continue;
        la->set_tooltip_text(it->second);
        if (auto field = grid.get_child_at(1, row); field && field->get_tooltip_text().empty())
            field->set_tooltip_text(it->second);
    }
}

namespace {
const std::map<std::string, std::string> menu_hints = {
        // View menu
        {"Selection filter", "Choose which kinds of things (points, lines, faces...) can be picked"},
        {"Clipping planes", "Cut the view with planes to look inside objects; nothing is changed in the model"},
        {"Previous construction entities", "Also show helper lines and points from earlier steps"},
        {"Only solid models", "Hide sketch lines, show just the finished solids"},
        {"Hide irrelevant workplanes", "Hide drawing planes that have nothing to do with the current step"},
        {"Perspective projection", "Far things look smaller, like a camera; off gives a flat technical view"},
        {"Studio lighting", "Soft studio light in the view instead of flat shading"},
        {"Sizes on the object", "Show Width, Depth and Height on the picked object while the pointer is over it; click a size to type a new one"},
        {"Sketch lines of other objects",
         "Show the sketch lines (the drawing a shape is made from) of every object, not just the one being edited"},
        {"Simple toolbars (no documents)", "A simpler window: no list of documents, just the objects"},
        {"One document per screen", "Show only the current document in the 3D view"},
        {"Hidden documents in the list", "Also list documents that are not shown in this tab"},
        {"Grid size", "Distance between grid lines; Automatic follows the zoom"},
        {"Snap to grid", "Make points and moves land on grid lines"},
        {"Grid and floor settings…", "How big the floor is and how the grid looks"},
        // main menu
        {"Check for updates", "Look for a newer version now"},
        {"Check automatically at start", "Look for a newer version every time the program starts"},
        {"Back up unsaved work", "Keep a copy of unsaved documents so a crash doesn't lose them"},
        {"Folder for documents...", "Where Open and Save start, and where backups are kept"},
        {"Website", "Open the project's web page"},
        {"Source code on GitHub", "Open the program's source code page"},
        {"Report a problem", "Open the page where you can tell us about a bug or idea"},
        {"Preferences", "Mouse, keys, colours and other settings"},
        {"Logger", "Messages the program writes while it runs, useful for finding problems"},
        {"About", "Version and credits"},
        {"Off", "No backups"},
};

void set_menu_hints(Gtk::Widget &w)
{
    for (auto ch = w.get_first_child(); ch; ch = ch->get_next_sibling()) {
        // menu items are GtkModelButtons: private, but their "text" is the label
        if (std::string(G_OBJECT_TYPE_NAME(ch->gobj())) == "GtkModelButton" && ch->get_tooltip_text().empty()) {
            gchar *text = nullptr;
            g_object_get(ch->gobj(), "text", &text, nullptr);
            if (text) {
                const auto it = menu_hints.find(text);
                if (it != menu_hints.end())
                    ch->set_tooltip_text(it->second);
                g_free(text);
            }
        }
        set_menu_hints(*ch);
    }
}
} // namespace

void add_menu_hints(Gtk::Popover &popover)
{
    popover.signal_map().connect([&popover] { set_menu_hints(popover); });
}

std::string get_action_hint(const ActionToolID &id)
{
    static const std::map<ActionToolID, std::string> hints = {
            {ActionID::ADD_BOX, "A box; change its sizes in the panel or with the handles"},
            {ActionID::ADD_CYLINDER, "A round column"},
            {ActionID::ADD_SPHERE, "A ball"},
            {ActionID::ADD_CONE, "A cone"},
            {ActionID::ADD_WEDGE, "A ramp: a box cut in half along its diagonal"},
            {ActionID::ADD_TEXT, "Raised letters that say what you type"},
            {ActionID::ADD_PICTURE_OBJECT, "A thin plate with a picture on it, like a photo print"},
            {ActionID::MAKE_FROM_DESCRIPTION, "Describe an object in words and let an AI build it"},
            {ActionID::ADD_TUBE, "A pipe: a cylinder with a round hole through it"},
            {ActionID::ADD_TORUS, "A ring, like a doughnut"},
            {ActionID::ADD_HEXAGON, "A six-sided block, like a nut"},
            {ActionID::ADD_TRIANGLE, "A block with a triangle outline"},
            {ActionID::ADD_STAR, "A star-shaped block"},
            {ActionID::ADD_HEART, "A heart-shaped block"},
            {ActionID::ADD_HALF_SPHERE, "Half a ball, flat side down"},
            {ActionID::ADD_SCREW, "A screw or bolt: choose the head, size and length (the ridges are rings, not a spiral)"},
            {ActionID::ADD_HOLE_SHAPE, "A round hole: move it into an object, then press Cut in the panel"},
            {ActionID::ADD_BOWL, "A hollow, rounded bowl"},
            {ActionID::ADD_VASE, "A hollow vase"},
            {ActionID::ADD_LAMP, "A light that shines all around from one point"},
            {ActionID::ADD_SPOTLIGHT, "A light that shines in a cone, like a stage light"},
            {ActionID::ADD_SUN, "Light from far away in one direction, like sunlight"},
            {ToolID::SKETCH_ON_FACE, "Start a drawing on a flat face of an object; click the face"},
            {ActionID::CREATE_GROUP_SKETCH, "Start a new flat drawing (sketch) to turn into a shape later"},
            {ToolID::DRAW_CONTOUR, "Draw connected lines and arcs; Esc to stop"},
            {ToolID::DRAW_RECTANGLE, "Draw a rectangle: click two corners"},
            {ToolID::DRAW_CIRCLE_2D, "Draw a circle: click the middle, then the edge"},
            {ToolID::DRAW_REGULAR_POLYGON, "Draw a shape with equal sides, like a hexagon"},
            {ToolID::DRAW_TEXT, "Write text in the sketch, to extrude later"},
            {ToolID::DRAW_WORKPLANE, "Add a flat plane to draw on"},
            {ActionID::CREATE_GROUP_EXTRUDE, "Pull the closed outline of a sketch up into a solid"},
            {ActionID::CREATE_GROUP_REVOLVE, "Turn the outline of a sketch around a line, like on a lathe"},
            {ActionID::CREATE_GROUP_LOFT, "A solid that blends from one outline to another"},
            {ActionID::CREATE_GROUP_FILLET, "Round the picked edges"},
            {ActionID::CREATE_GROUP_CHAMFER, "Cut the picked edges at an angle"},
            {ActionID::COMBINE_OBJECTS,
             "Join objects into one, cut one out of another, or keep only where they overlap; click an object, "
             "then pick the other one in the panel"},
    };
    if (auto it = hints.find(id); it != hints.end())
        return it->second;
    return {};
}

} // namespace dune3d

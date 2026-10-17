#include "editor.hpp"
#include "action_search.hpp"
#include "dune3d_appwindow.hpp"
#include "workspace_browser.hpp"
#include "core/tool_id.hpp"
#include "action/action_id.hpp"
#include "action/action.hpp"

namespace dune3d {

/*
 * Find (Ctrl+K): everything worth looking for, with where it lives in the
 * window, so it can be found there next time. Keywords include Hebrew words.
 */

void Editor::focus_object_panel_field(const std::string &label)
{
    if (!m_object_editor || m_object_editor_light) {
        m_workspace_browser->show_toast("Click an object first - \"" + label + "\" is in its panel");
        return;
    }
    auto grid = dynamic_cast<Gtk::Grid *>(m_object_editor);
    if (!grid)
        return;
    for (auto ch = grid->get_first_child(); ch; ch = ch->get_next_sibling()) {
        auto la = dynamic_cast<Gtk::Label *>(ch);
        if (la && la->get_text().raw() == label) {
            int col, row, w, h;
            gtk_grid_query_child(grid->gobj(), GTK_WIDGET(la->gobj()), &col, &row, &w, &h);
            auto field = grid->get_child_at(col + 1, row);
            if (field == la) // a heading across the panel: what's under it
                field = grid->get_child_at(0, row + 1);
            if (field)
                field->child_focus(Gtk::DirectionType::TAB_FORWARD);
            return;
        }
        // buttons spanning the row, like "Add sticker..."
        if (auto bu = dynamic_cast<Gtk::Button *>(ch); bu && bu->get_label().raw() == label) {
            bu->grab_focus();
            return;
        }
    }
    m_workspace_browser->show_toast("This object has no \"" + label + "\"");
}

std::vector<SearchItem> Editor::get_search_items()
{
    std::vector<SearchItem> items;
    auto action = [this](ActionToolID id) { return [this, id] { trigger_action(id); }; };
    // its keyboard shortcut, if it has one (Preferences > Keys)
    auto keys = [this](ActionToolID id) -> std::string {
        if (auto it = m_action_connections.find(id); it != m_action_connections.end() && it->second.key_sequences.size())
            return key_sequence_to_string(it->second.key_sequences.front());
        return {};
    };
    auto win_action = [this](const char *name) { return [this, name] { m_win.activate_action(name); }; };
    auto field = [this](const char *label) { return [this, label] { focus_object_panel_field(label); }; };
    auto object = [this](std::function<void(const UUID &, const UUID &)> fn) {
        return [this, fn] {
            if (!m_object_editor || m_object_editor_light) {
                m_workspace_browser->show_toast("Click an object first");
                return;
            }
            fn(m_object_editor_doc, m_object_editor_body);
        };
    };

    const char *shapes = "Tool palette > Shapes";
    items.push_back({"Box", shapes, "cube block קובייה תיבה", action(ActionID::ADD_BOX), keys(ActionID::ADD_BOX)});
    items.push_back({"Cylinder", shapes, "גליל", action(ActionID::ADD_CYLINDER), keys(ActionID::ADD_CYLINDER)});
    items.push_back({"Sphere", shapes, "ball כדור", action(ActionID::ADD_SPHERE), keys(ActionID::ADD_SPHERE)});
    items.push_back({"Cone", shapes, "חרוט", action(ActionID::ADD_CONE), keys(ActionID::ADD_CONE)});
    items.push_back({"Tube", shapes, "pipe ring צינור", action(ActionID::ADD_TUBE), keys(ActionID::ADD_TUBE)});
    items.push_back({"Torus", shapes, "ring donut טבעת", action(ActionID::ADD_TORUS), keys(ActionID::ADD_TORUS)});
    items.push_back({"Wedge", shapes, "ramp טריז", action(ActionID::ADD_WEDGE), keys(ActionID::ADD_WEDGE)});
    items.push_back({"Hexagon", shapes, "hex nut prism משושה", action(ActionID::ADD_HEXAGON), keys(ActionID::ADD_HEXAGON)});
    items.push_back({"Triangle", shapes, "roof prism משולש גג", action(ActionID::ADD_TRIANGLE), keys(ActionID::ADD_TRIANGLE)});
    items.push_back({"Star", shapes, "כוכב", action(ActionID::ADD_STAR), keys(ActionID::ADD_STAR)});
    items.push_back({"Heart", shapes, "לב", action(ActionID::ADD_HEART), keys(ActionID::ADD_HEART)});
    items.push_back({"Half sphere", shapes, "dome hemisphere חצי כדור כיפה", action(ActionID::ADD_HALF_SPHERE), keys(ActionID::ADD_HALF_SPHERE)});
    items.push_back({"Picture", shapes, "image photo flat sticker תמונה", action(ActionID::ADD_PICTURE_OBJECT), keys(ActionID::ADD_PICTURE_OBJECT)});
    items.push_back({"Screw", shapes, "bolt thread hex בורג ברגים", action(ActionID::ADD_SCREW), keys(ActionID::ADD_SCREW)});
    items.push_back({"Round hole", shapes, "drill cut subtract חור קידוח", action(ActionID::ADD_HOLE_SHAPE), keys(ActionID::ADD_HOLE_SHAPE)});
    items.push_back({"3D text", shapes, "letters words טקסט כיתוב", action(ActionID::ADD_TEXT), keys(ActionID::ADD_TEXT)});
    items.push_back({"Bowl", shapes, "dish קערה", action(ActionID::ADD_BOWL), keys(ActionID::ADD_BOWL)});
    items.push_back({"Vase", shapes, "bottle אגרטל", action(ActionID::ADD_VASE), keys(ActionID::ADD_VASE)});
    items.push_back({"Make from a description (AI)", "Tool palette > Shapes > Describe",
                     "ai text prompt describe chatgpt gemini claude openai chatbot copy paste בינה מלאכותית תיאור טקסט צאט בוט", action(ActionID::MAKE_FROM_DESCRIPTION), keys(ActionID::MAKE_FROM_DESCRIPTION)});

    const char *lights = "Tool palette > Lights";
    items.push_back({"Lamp", lights, "light bulb point תאורה מנורה אור", action(ActionID::ADD_LAMP), keys(ActionID::ADD_LAMP)});
    items.push_back({"Spotlight", lights, "light spot זרקור תאורה", action(ActionID::ADD_SPOTLIGHT), keys(ActionID::ADD_SPOTLIGHT)});
    items.push_back({"Sun", lights, "light daylight שמש תאורה", action(ActionID::ADD_SUN), keys(ActionID::ADD_SUN)});

    const char *sketch = "Tool palette > Sketch";
    items.push_back({"Sketch on a face", sketch, "on face draw שרטוט פאה", action(ToolID::SKETCH_ON_FACE), keys(ToolID::SKETCH_ON_FACE)});
    items.push_back({"New sketch", sketch, "draw שרטוט חדש", action(ActionID::CREATE_GROUP_SKETCH), keys(ActionID::CREATE_GROUP_SKETCH)});
    items.push_back({"Line", sketch, "draw קו", action(ToolID::DRAW_CONTOUR), keys(ToolID::DRAW_CONTOUR)});
    items.push_back({"Rectangle", sketch, "square מלבן", action(ToolID::DRAW_RECTANGLE), keys(ToolID::DRAW_RECTANGLE)});
    items.push_back({"Circle", sketch, "עיגול מעגל", action(ToolID::DRAW_CIRCLE_2D), keys(ToolID::DRAW_CIRCLE_2D)});
    items.push_back({"Polygon", sketch, "hexagon מצולע", action(ToolID::DRAW_REGULAR_POLYGON), keys(ToolID::DRAW_REGULAR_POLYGON)});
    items.push_back({"Text in a sketch", sketch, "letters טקסט", action(ToolID::DRAW_TEXT), keys(ToolID::DRAW_TEXT)});
    items.push_back({"Plane", sketch, "workplane מישור", action(ToolID::DRAW_WORKPLANE), keys(ToolID::DRAW_WORKPLANE)});

    const char *solid = "Tool palette > Solid";
    items.push_back({"Extrude", solid, "pull push height שליפה הבלטה", action(ActionID::CREATE_GROUP_EXTRUDE), keys(ActionID::CREATE_GROUP_EXTRUDE)});
    items.push_back({"Revolve", solid, "lathe turn סיבוב", action(ActionID::CREATE_GROUP_REVOLVE), keys(ActionID::CREATE_GROUP_REVOLVE)});
    items.push_back({"Loft", solid, "between shapes מעבר", action(ActionID::CREATE_GROUP_LOFT), keys(ActionID::CREATE_GROUP_LOFT)});
    items.push_back({"Fillet edges", solid, "round עיגול פינות", action(ActionID::CREATE_GROUP_FILLET), keys(ActionID::CREATE_GROUP_FILLET)});
    items.push_back({"Chamfer edges", solid, "bevel שיפוע", action(ActionID::CREATE_GROUP_CHAMFER), keys(ActionID::CREATE_GROUP_CHAMFER)});
    items.push_back({"Combine objects", solid, "join subtract boolean איחוד חיסור", action(ActionID::COMBINE_OBJECTS), keys(ActionID::COMBINE_OBJECTS)});

    const char *panel = "Object panel (click an object first)";
    auto in_panel = [panel](const char *what) { return std::string(panel) + " > " + what; };
    items.push_back({"Colour", in_panel("Colour"), "color צבע", field("Colour")});
    items.push_back({"Material", in_panel("Material"), "metal glass glossy matte חומר מתכת זכוכית מבריק",
                     field("Material")});
    items.push_back({"Hollow out", in_panel("Hollow"), "shell wall cup חלול דופן", field("Hollow")});
    items.push_back({"Round edges", in_panel("Round edges"), "fillet עיגול קצוות", field("Round edges")});
    items.push_back({"Add a sticker", in_panel("Stickers > Add sticker..."),
                     "decal label picture image logo מדבקה תמונה לוגו",
                     object([this](const UUID &d, const UUID &b) { choose_decal_picture(d, b); })});
    items.push_back({"Duplicate", in_panel("Duplicate") + " - or Ctrl+D", "copy שכפול העתק",
                     action(ActionID::DUPLICATE_OBJECT), keys(ActionID::DUPLICATE_OBJECT)});
    items.push_back({"Join, subtract, intersect", in_panel("Combine"), "boolean איחוד חיסור חיתוך", field("With")});
    items.push_back({"Size and position", in_panel("Dimensions") + " - or drag the handles on the object",
                     "dimensions width height diameter move מידות גודל גובה רוחב מיקום", field("Width")});
    items.push_back({"Change a part of a joined object", in_panel("Parts") + " - alike parts change together",
                     "parts legs table component חלקים רגליים שולחן חלק", field("Parts")});
    items.push_back({"Rename an object", in_panel("Name"), "name שם", field("Name")});
    items.push_back({"Hide or show an object", in_panel("Visible") + " - or its tick in the tree on the left",
                     "visible hide הסתר הצג", field("Visible")});

    items.push_back({"Import objects", "Header bar > Import", "step stl obj gltf glb open file ייבוא קובץ",
                     [this] { on_import_object(); }});
    items.push_back({"Render a picture", "Header bar > Render", "photo image picture blender רינדור תמונה הדמיה",
                     [this] { show_render_window(); }});
    const char *exp = "Header bar > Export";
    items.push_back({"Export for 3D printing (STL)", exp, "stl print הדפסה ייצוא",
                     action(ActionID::EXPORT_ALL_SOLID_MODELS_STL), keys(ActionID::EXPORT_ALL_SOLID_MODELS_STL)});
    items.push_back({"Export for CAD (STEP)", exp, "step ייצוא", action(ActionID::EXPORT_ALL_SOLID_MODELS_STEP), keys(ActionID::EXPORT_ALL_SOLID_MODELS_STEP)});
    items.push_back({"Export with colours and materials (glTF)", exp, "gltf glb blender powerpoint ייצוא חומרים",
                     action(ActionID::EXPORT_ALL_GLTF), keys(ActionID::EXPORT_ALL_GLTF)});
    items.push_back({"Save", "Header bar > Save - or Ctrl+S", "שמירה", action(ActionID::SAVE), keys(ActionID::SAVE)});
    items.push_back({"Open a document", "Header bar > Open - or Ctrl+O", "load פתיחה", action(ActionID::OPEN_DOCUMENT), keys(ActionID::OPEN_DOCUMENT)});
    items.push_back({"New document", "Header bar, next to Open - or Ctrl+N", "חדש", action(ActionID::NEW_DOCUMENT), keys(ActionID::NEW_DOCUMENT)});
    items.push_back({"Undo", "Header bar > arrow left - or Ctrl+Z", "ביטול", action(ActionID::UNDO), keys(ActionID::UNDO)});
    items.push_back({"Redo", "Header bar > arrow right - or Ctrl+Y", "חזרה", action(ActionID::REDO), keys(ActionID::REDO)});

    const char *viewbar = "View bar, top right under the cube";
    items.push_back({"Show everything", std::string(viewbar) + " > Fit", "zoom fit הכל זום", action(ActionID::VIEW_FIT), keys(ActionID::VIEW_FIT)});
    items.push_back({"3D view", std::string(viewbar) + " > 3D", "iso תלת", action(ActionID::VIEW_ISO), keys(ActionID::VIEW_ISO)});
    items.push_back({"View from above", std::string(viewbar) + " > Top", "top מלמעלה", action(ActionID::VIEW_TOP), keys(ActionID::VIEW_TOP)});
    items.push_back({"View from the front", std::string(viewbar) + " > Front", "front מלפנים",
                     action(ActionID::VIEW_FRONT), keys(ActionID::VIEW_FRONT)});
    items.push_back({"View from the right", std::string(viewbar) + " > Right", "right מימין",
                     action(ActionID::VIEW_RIGHT), keys(ActionID::VIEW_RIGHT)});
    const char *viewmenu = "Status bar at the bottom > View";
    items.push_back({"Grid size and snapping", std::string(viewmenu) + " > Grid size", "grid snap רשת הצמדה",
                     win_action("win.grid_settings")});
    items.push_back({"Studio lighting on or off", std::string(viewmenu) + " > Studio lighting", "light תאורה",
                     win_action("win.studio_lighting")});
    items.push_back({"Perspective view on or off", std::string(viewmenu) + " > Perspective projection",
                     "perspective orthographic פרספקטיבה", win_action("win.perspective")});
    items.push_back({"Show other objects' sketch lines", std::string(viewmenu) + " > Sketch lines of other objects",
                     "sketch lines קווי שרטוט", win_action("win.show_other_sketches")});
    items.push_back({"One document per screen (no sharing between tabs)",
                     std::string(viewmenu) + " > One document per screen",
                     "documents screens tabs one per screen מסמך אחד מסך לשונית",
                     win_action("win.one_document_per_screen")});
    items.push_back({"List documents hidden from the view",
                     std::string(viewmenu) + " > Hidden documents in the list",
                     "documents files open מסמכים קבצים פתוחים", win_action("win.show_other_documents")});
    items.push_back({"Show only the solids", std::string(viewmenu) + " > Only solid models", "solids only מוצקים",
                     win_action("win.show_only_solid_models")});

    const char *menu = "Header bar > menu button (three lines)";
    items.push_back({"Check for updates", std::string(menu) + " > Check for updates", "update version עדכון",
                     win_action("win.check_updates")});
    items.push_back({"Preferences", std::string(menu) + " > Preferences", "settings keys colours הגדרות",
                     win_action("app.preferences")});
    items.push_back({"About", std::string(menu) + " > About", "version credits אודות", win_action("app.about")});
    items.push_back({"Website", std::string(menu) + " > Website", "site help download אתר עזרה",
                     win_action("win.open_website")});
    items.push_back({"Source code on GitHub", std::string(menu) + " > Source code on GitHub", "github code קוד מקור",
                     win_action("win.open_source")});
    items.push_back({"Report a problem", std::string(menu) + " > Report a problem", "bug issue feedback באג בעיה דיווח",
                     win_action("win.report_problem")});
    return items;
}

void Editor::show_search(const std::string &text)
{
    if (!m_search_window)
        m_search_window = std::make_unique<ActionSearchWindow>(m_win, [this] { return get_search_items(); });
    m_search_window->open(text);
}

} // namespace dune3d

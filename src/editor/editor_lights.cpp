#include "editor.hpp"
#include "group_editor/group_editor.hpp"
#include "dune3d_appwindow.hpp"
#include "workspace_browser.hpp"
#include "canvas/canvas.hpp"
#include "document/document.hpp"
#include "document/light.hpp"
#include "document/body_solids.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include "widgets/spin_button_dim.hpp"
#include "util/gtk_util.hpp"
#include "panel_hints.hpp"
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <glm/gtx/rotate_vector.hpp>

namespace dune3d {

/*
 * Lights: added from the tool palette, picked by clicking their marker in the
 * 3D view, edited in a panel like the object panel.
 */

namespace {
struct SceneBounds {
    glm::dvec3 center = {0, 0, 0};
    double size = 50;
};
} // namespace

static SceneBounds get_scene_bounds(const Document &doc, const BodyFilter &filter)
{
    Bnd_Box box;
    for (const auto &bs : get_body_solids(doc, UUID(), filter))
        BRepBndLib::Add(bs.solid->m_shape_acc, box, true);
    if (box.IsVoid())
        return {};
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    return {.center = {(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2},
            .size = std::max({x1 - x0, y1 - y0, z1 - z0, 10.})};
}

std::vector<CanvasLight> Editor::get_canvas_lights(const Document &doc) const
{
    std::vector<CanvasLight> r;
    for (const auto &light : doc.m_lights) {
        CanvasLight l;
        l.type = static_cast<CanvasLight::Type>(light.m_type);
        l.position = light.m_position;
        l.direction = light.get_direction();
        l.color = glm::vec3(light.m_color.r, light.m_color.g, light.m_color.b) * static_cast<float>(light.m_brightness);
        l.distance = light.get_target_distance();
        l.spot_angle = light.m_spot_angle;
        r.push_back(l);
    }
    return r;
}

void Editor::add_light(Light::Type type)
{
    if (m_core.tool_is_active() || !m_core.has_documents())
        return;
    auto &doc = m_core.get_current_document();
    const auto bounds = get_scene_bounds(doc, [this](const UUID &b) { return body_is_shown(b); });

    Light light;
    light.m_type = type;
    light.m_name = get_light_type_label(type);
    // above, in front of and to the left of the objects as seen now, aimed at them
    const auto q = glm::dquat(get_canvas().get_cam_quat());
    const auto right = q * glm::dvec3(1, 0, 0);
    const auto back = q * glm::dvec3(0, 0, 1);
    auto dir = glm::normalize(-.7 * right + .8 * back);
    // more lights go around the objects rather than in the same spot
    dir = glm::rotateZ(glm::dvec3(dir.x, dir.y, 0), glm::radians(120. * doc.m_lights.size()));
    dir = glm::normalize(dir + glm::dvec3(0, 0, 1.1));
    const double dist = bounds.size * 2;
    light.m_target = glm::round(bounds.center);
    light.m_position = glm::round(bounds.center + dir * dist);
    if (type == Light::Type::SPOT)
        light.m_spot_angle = 40;
    if (type == Light::Type::SUN)
        light.m_color = {1, .96, .9};

    const auto uu = light.m_uuid;
    doc.m_lights.push_back(light);
    m_core.set_needs_save();
    m_core.rebuild("add light");
    canvas_update_keep_selection();
    show_light_editor(m_core.get_current_idocument_info().get_uuid(), uu);
}

static Light *find_light(Document &doc, const UUID &uu)
{
    for (auto &l : doc.m_lights) {
        if (l.m_uuid == uu)
            return &l;
    }
    return nullptr;
}

bool Editor::pick_light_under_cursor()
{
    if (!m_core.has_documents())
        return false;
    auto &doc = m_core.get_current_document();
    const auto cursor = get_canvas().get_cursor_pos_win();
    const auto near_cursor = [this, &cursor](const glm::dvec3 &pos) {
        return glm::length(get_canvas().project_to_window(pos) - glm::dvec2(cursor)) < 14;
    };
    for (const auto &light : doc.m_lights) {
        const bool lamp = near_cursor(light.m_position);
        if (!lamp && !(light.m_type != Light::Type::POINT && near_cursor(light.m_target)))
            continue;
        const auto doc_uu = m_core.get_current_idocument_info().get_uuid();
        show_light_editor(doc_uu, light.m_uuid);
        // grabbed: it follows the pointer on a plane facing the camera
        LightDrag drag;
        drag.doc = doc_uu;
        drag.light = light.m_uuid;
        drag.target = !lamp;
        drag.origin = lamp ? light.m_position : light.m_target;
        drag.normal = glm::dquat(get_canvas().get_cam_quat()) * glm::dvec3(0, 0, 1);
        drag.offset = drag.origin - get_canvas().get_cursor_pos_for_plane(drag.origin, drag.normal);
        m_light_drag = drag;
        get_canvas().inhibit_drag_selection();
        return true;
    }
    return false;
}

void Editor::update_light_drag()
{
    if (!m_light_drag)
        return;
    auto &drag = *m_light_drag;
    auto &doc = m_core.get_idocument_info(drag.doc).get_document();
    auto light = find_light(doc, drag.light);
    if (!light) {
        m_light_drag.reset();
        return;
    }
    const auto pos = glm::round(get_canvas().get_cursor_pos_for_plane(drag.origin, drag.normal) + drag.offset);
    auto &point = drag.target ? light->m_target : light->m_position;
    if (point == pos)
        return;
    point = pos;
    drag.moved = true;
    canvas_update_keep_selection();
}

void Editor::end_light_drag()
{
    if (!m_light_drag)
        return;
    const auto drag = *m_light_drag;
    m_light_drag.reset();
    if (!drag.moved)
        return;
    m_core.set_needs_save();
    m_core.rebuild(drag.target ? "aim light" : "move light"); // the panel's fields refresh on rebuild
    canvas_update_keep_selection();
}

void Editor::delete_light(const UUID &doc_uu, const UUID &light_uu)
{
    auto &doc = m_core.get_idocument_info(doc_uu).get_document();
    std::erase_if(doc.m_lights, [&light_uu](const auto &l) { return l.m_uuid == light_uu; });
    m_core.set_needs_save();
    m_core.rebuild("delete light");
    canvas_update_keep_selection();
    if (m_object_editor) {
        remove_from_side_panel(*m_object_editor);
        m_object_editor = nullptr;
    }
    m_object_editor_light = UUID();
    update_group_editor();
}

void Editor::show_light_editor(const UUID &doc_uu, const UUID &light_uu)
{
    if (m_group_editor) {
        if (m_delayed_commit_connection.connected())
            commit_from_editor();
        remove_from_side_panel(*m_group_editor);
        m_group_editor = nullptr;
    }
    if (m_object_editor) {
        remove_from_side_panel(*m_object_editor);
        m_object_editor = nullptr;
    }
    auto &doc = m_core.get_idocument_info(doc_uu).get_document();
    auto light = find_light(doc, light_uu);
    if (!light)
        return;
    m_object_editor_doc = doc_uu;
    m_object_editor_body = UUID();
    m_object_editor_light = light_uu;
    m_object_combine_dropdown = nullptr;

    auto grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(8);
    grid->set_column_spacing(10);
    grid->set_margin(14);
    grid->add_css_class("object-editor");
    m_object_editor = grid;
    int top = 0;

    auto heading = [grid, &top](const std::string &text) {
        auto la = Gtk::make_managed<Gtk::Label>(text);
        la->set_xalign(0);
        la->add_css_class("object-editor-heading");
        grid->attach(*la, 0, top++, 2, 1);
    };
    // applies a change to the light, if it's still there
    auto edit = [this, doc_uu, light_uu](const char *what, auto fn, bool rebuild_panel = false) {
        auto &d = m_core.get_idocument_info(doc_uu).get_document();
        auto l = find_light(d, light_uu);
        if (!l)
            return;
        if (!fn(*l))
            return; // unchanged
        m_core.set_needs_save();
        m_object_editor_keep = !rebuild_panel;
        m_core.rebuild(what);
        canvas_update_keep_selection();
        if (rebuild_panel)
            Glib::signal_idle().connect_once([this, doc_uu, light_uu] { show_light_editor(doc_uu, light_uu); });
    };

    heading("Light");
    {
        auto en = Gtk::make_managed<Gtk::Entry>();
        en->set_text(light->m_name);
        en->set_hexpand(true);
        en->signal_activate().connect([en, edit] {
            edit("rename light", [en](Light &l) {
                if (l.m_name == en->get_text().raw())
                    return false;
                l.m_name = en->get_text();
                return true;
            });
        });
        grid_attach_label_and_widget(*grid, "Name", *en, top);
    }
    {
        auto items = Gtk::StringList::create();
        for (const auto t : {Light::Type::POINT, Light::Type::SPOT, Light::Type::SUN})
            items->append(get_light_type_label(t));
        auto dd = Gtk::make_managed<Gtk::DropDown>(items);
        dd->set_selected(static_cast<guint>(light->m_type));
        dd->set_tooltip_text("A lamp shines all around from where it is, a spotlight in a cone. The sun is so far away that its rays are all parallel: only its direction matters, not how far it is");
        dd->property_selected().signal_changed().connect([dd, edit] {
            edit(
                    "light type",
                    [dd](Light &l) {
                        const auto t = static_cast<Light::Type>(dd->get_selected());
                        if (l.m_type == t)
                            return false;
                        l.m_type = t;
                        return true;
                    },
                    true);
        });
        grid_attach_label_and_widget(*grid, "Type", *dd, top);
    }
    {
        auto dia = Gtk::ColorDialog::create();
        dia->set_with_alpha(false);
        auto bu = Gtk::make_managed<Gtk::ColorDialogButton>(dia);
        bu->set_hexpand(true);
        bu->set_rgba(rgba_from_color(light->m_color));
        bu->property_rgba().signal_changed().connect([bu, edit] {
            edit("light colour", [bu](Light &l) {
                l.m_color = color_from_rgba(bu->get_rgba());
                return true;
            });
        });
        grid_attach_label_and_widget(*grid, "Colour", *bu, top);
    }
    {
        auto sp = Gtk::make_managed<Gtk::SpinButton>();
        sp->set_range(0, 100);
        sp->set_increments(.1, 1);
        sp->set_digits(2);
        sp->set_value(light->m_brightness);
        sp->set_hexpand(true);
        sp->set_tooltip_text("1 lights what it's aimed at about as much as the normal view lighting");
        sp->signal_value_changed().connect([sp, edit] {
            edit("light brightness", [sp](Light &l) {
                if (l.m_brightness == sp->get_value())
                    return false;
                l.m_brightness = sp->get_value();
                return true;
            });
        });
        grid_attach_label_and_widget(*grid, "Brightness", *sp, top).add_css_class("tnum");
    }
    if (light->m_type == Light::Type::SPOT) {
        auto sp = Gtk::make_managed<Gtk::SpinButton>();
        sp->set_range(1, 179);
        sp->set_increments(5, 15);
        sp->set_digits(0);
        sp->set_value(light->m_spot_angle);
        sp->set_hexpand(true);
        sp->set_tooltip_text("How wide the cone of light is, in degrees");
        sp->signal_value_changed().connect([sp, edit] {
            edit("spotlight angle", [sp](Light &l) {
                if (l.m_spot_angle == sp->get_value())
                    return false;
                l.m_spot_angle = sp->get_value();
                return true;
            });
        });
        grid_attach_label_and_widget(*grid, "Cone angle", *sp, top).add_css_class("tnum");
    }

    auto add_xyz = [grid, &top, edit](glm::dvec3 value, glm::dvec3 Light::*member, const char *what) {
        for (int axis = 0; axis < 3; axis++) {
            auto sp = Gtk::make_managed<SpinButtonDim>();
            sp->set_range(-1e6, 1e6);
            sp->set_increments(1, 10);
            sp->set_value(value[axis]);
            sp->set_hexpand(true);
            sp->signal_value_changed().connect([sp, edit, member, axis, what] {
                edit(what, [sp, member, axis](Light &l) {
                    if ((l.*member)[axis] == sp->get_value())
                        return false;
                    (l.*member)[axis] = sp->get_value();
                    return true;
                });
            });
            grid_attach_label_and_widget(*grid, std::string(1, "XYZ"[axis]), *sp, top).add_css_class("tnum");
        }
    };
    heading("Position");
    add_xyz(light->m_position, &Light::m_position, "move light");
    if (light->m_type != Light::Type::POINT) {
        heading("Aimed at");
        add_xyz(light->m_target, &Light::m_target, "aim light");
    }
    {
        auto bu = Gtk::make_managed<Gtk::Button>(light->m_type == Light::Type::POINT ? "Move closer to the objects"
                                                                                      : "Aim at the objects");
        bu->set_tooltip_text(light->m_type == Light::Type::POINT
                                     ? "Keeps it at the same angle, at a distance that suits the objects"
                                     : "Points it at the middle of the objects that are shown");
        bu->signal_clicked().connect([this, edit] {
            const auto bounds = get_scene_bounds(m_core.get_current_document(),
                                                 [this](const UUID &b) { return body_is_shown(b); });
            edit(
                    "aim light",
                    [&bounds](Light &l) {
                        auto dir = l.m_position - l.m_target;
                        if (glm::length(dir) < 1e-6)
                            dir = {0, 0, 1};
                        l.m_target = glm::round(bounds.center);
                        if (l.m_type == Light::Type::POINT)
                            l.m_position = glm::round(l.m_target + glm::normalize(dir) * bounds.size * 2.);
                        return true;
                    },
                    true);
        });
        grid->attach(*bu, 0, top++, 2, 1);
    }
    {
        auto del = Gtk::make_managed<Gtk::Button>("Delete");
        del->add_css_class("destructive-action");
        del->set_margin_top(10);
        del->signal_clicked().connect([this, doc_uu, light_uu] {
            Glib::signal_idle().connect_once([this, doc_uu, light_uu] { delete_light(doc_uu, light_uu); });
        });
        grid->attach(*del, 0, top++, 2, 1);
    }

    add_panel_hints(*grid);
    m_group_editor_box->append(*grid);
    get_canvas().grab_focus();
    canvas_update_keep_selection();
}

} // namespace dune3d

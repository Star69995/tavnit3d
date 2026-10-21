#include "editor.hpp"
#include "document/object_dimensions.hpp"
#include "dune3d_appwindow.hpp"
#include "workspace_browser.hpp"
#include "canvas/canvas.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/group/group_fillet.hpp"
#include "document/hollow.hpp"
#include "document/round_edges.hpp"
#include "document/body_solids.hpp"
#include "document/duplicate.hpp"
#include "document/face_sketch.hpp"
#include "document/object_parts.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint.hpp"
#include "document/constraint/iconstraint_datum.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include <algorithm>
#include <ranges>
#include <limits>

namespace dune3d {

/*
 * Handles on the picked object, for changing it by dragging: on top for the
 * height, on the sides facing the camera for the width (or diameter, radius)
 * and depth, inside the top for a tube's hole, and two at the top edge: one
 * for the wall thickness (hollowing it out) and one for rounding the edges.
 * They change the same things as the object panel; the face under a size
 * handle follows the pointer.
 */

std::vector<Editor::ObjectHandle> Editor::get_object_handles()
{
    std::vector<ObjectHandle> handles;
    m_handle_box.reset();
    if (!m_object_editor || m_object_editor_light || !m_object_editor_body || m_core.tool_is_active())
        return handles;
    if (!m_core.has_documents() || m_core.get_current_idocument_info().get_uuid() != m_object_editor_doc)
        return handles;
    auto &doc = m_core.get_current_idocument_info().get_document();
    if (!doc.get_groups().contains(m_object_editor_body))
        return handles;
    // nothing to grab on an object that is hidden (eye / solid switch in the list)
    if (!body_is_shown(m_object_editor_body))
        return handles;
    // a part of a joined object, when one is picked in the panel
    const auto body = m_object_editor_part && doc.get_groups().contains(m_object_editor_part) ? m_object_editor_part
                                                                                              : m_object_editor_body;
    const auto dims = get_object_dimensions(doc, body);
    auto find = [&dims](const char *label) -> const ObjectDimension * {
        for (const auto &d : dims) {
            if (d.label == label)
                return &d;
        }
        return nullptr;
    };

    // the frame the object stands in: its sketch's workplane
    const auto &body_group = doc.get_group(body);
    auto wrkpl = doc.get_entity_ptr<EntityWorkplane>(body_group.m_active_wrkpl);
    if (!wrkpl)
        return handles;
    const auto origin = wrkpl->m_origin;
    const auto ax = wrkpl->transform_relative({1, 0});
    const auto ay = wrkpl->transform_relative({0, 1});
    const auto az = wrkpl->get_normal_vector();

    const SolidModelOcc *solid = nullptr;
    for (const auto &bs : get_body_solids(doc, m_core.get_current_group(),
                                          [&body](const UUID &b) { return b == body; }))
        solid = bs.solid;
    if (!solid)
        return handles;
    glm::dvec3 lo{std::numeric_limits<double>::infinity()}, hi{-std::numeric_limits<double>::infinity()};
    for (const auto &face : solid->m_faces) {
        for (const auto &v : face.vertices) {
            const auto rel = glm::dvec3(v.x, v.y, v.z) - origin;
            const glm::dvec3 l{glm::dot(rel, ax), glm::dot(rel, ay), glm::dot(rel, az)};
            lo = glm::min(lo, l);
            hi = glm::max(hi, l);
        }
    }
    if (lo.x > hi.x)
        return handles;
    const auto c = (lo + hi) / 2.;
    m_handle_box.emplace();
    for (int i = 0; i < 8; i++) {
        const glm::dvec3 l{i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z};
        (*m_handle_box)[i] = origin + ax * l.x + ay * l.y + az * l.z;
    }
    const double size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
    const double out = std::max(size * .3, 4.); // how far the handle sits out from the face (room for its arrow)
    auto world = [&](const glm::dvec3 &l) { return origin + ax * l.x + ay * l.y + az * l.z; };
    auto add_dim = [&](const ObjectDimension *d, glm::dvec3 face, const glm::dvec3 &dir, double per_mm) {
        if (!d)
            return;
        ObjectHandle h;
        h.face = world(face);
        h.dir = dir;
        h.pos = h.face; // right on the box, like Tinkercad's squares
        h.constraint = d->constraint;
        h.group = d->group;
        h.scale = d->scale;
        h.per_mm = per_mm;
        h.label = d->label;
        handles.push_back(h);
    };

    // the sides facing the camera, so the handles aren't hidden behind the object;
    // centred things grow on both sides, so either side works
    const auto towards_eye = glm::dvec3(glm::rotate(get_canvas().get_cam_quat(), glm::vec3(0, 0, 1)));
    const bool right = glm::dot(ax, towards_eye) >= 0;
    const bool back = glm::dot(ay, towards_eye) >= 0;
    const auto dx = right ? ax : -ax;
    // the size handles sit on the bottom edges, in the middle of the sides facing the camera
    const glm::dvec3 side_x{right ? hi.x : lo.x, c.y, lo.z};
    const glm::dvec3 side_y{c.x, back ? hi.y : lo.y, lo.z};
    const double sx = right ? 1 : -1; // towards the shown side, in the object's own x

    // up: the height; a ring (no height) grows by its tube's diameter
    if (auto d = find("Height"))
        add_dim(d, {c.x, c.y, hi.z}, az, 1);
    else if (find("Radius") && find("Diameter"))
        add_dim(find("Diameter"), {c.x, c.y, hi.z}, az, 1);
    // sideways: width, or how wide round things are; they're centred, so the far side moves too
    if (auto d = find("Width"))
        add_dim(d, side_x, dx, 2);
    else if (auto d = find("Outer diameter"))
        add_dim(d, side_x, dx, 2);
    else if (auto d = find("Radius"))
        add_dim(d, side_x, dx, 1);
    else if (auto d = find("Diameter"))
        add_dim(d, side_x, dx, 2);
    // front to back: the depth
    if (auto d = find("Depth"))
        add_dim(d, side_y, back ? ay : -ay, 2);
    // a tube's hole: on its rim at the top, pulled outwards makes it wider
    if (auto d = find("Inner diameter")) {
        ObjectHandle h;
        h.face = world({c.x + sx * d->value / 2, c.y, hi.z});
        h.dir = dx;
        h.pos = h.face + az * out * .5;
        h.constraint = d->constraint;
        h.group = d->group;
        h.scale = d->scale;
        h.per_mm = 2;
        h.label = d->label;
        handles.push_back(h);
    }

    // lifting: on the shown side at the underside, dragged up or down; only for whole objects
    // that can be moved (a part of a joined object is moved by the whole)
    if (!m_object_editor_part && can_move_object(doc, body)) {
        ObjectHandle h;
        h.kind = ObjectHandle::Kind::LIFT;
        // above the middle of the top, like Tinkercad's cone: it takes the whole object up and down
        h.pos = world({c.x, c.y, hi.z}) + az * out * .9;
        h.face = h.pos;
        h.dir = {0, 0, 1};
        h.size = out;
        h.label = "Lift";
        handles.push_back(h);

        // turning: a curved arrow round the top, on the side facing the camera; it turns about the
        // upright line through the middle (like Tinkercad's, but about the vertical only)
        ObjectHandle r;
        r.kind = ObjectHandle::Kind::ROTATE;
        const auto toward = glm::normalize(glm::dvec3(towards_eye.x, towards_eye.y, 0) + glm::dvec3(1e-9, 0, 0));
        const double radius = std::max({hi.x - lo.x, hi.y - lo.y}) * .62 + out * .1;
        r.face = world({c.x, c.y, hi.z}) + az * out * .35; // a point on the turning line, at the arrow's height
        r.pos = r.face + toward * radius;
        r.dir = az;
        r.size = radius;
        r.label = "Rotate";
        handles.push_back(r);
    }

    // walls and rounding, at the edge where the top meets the shown side
    const auto edge = world({right ? hi.x : lo.x, c.y, hi.z});
    {
        // dragged inwards, the walls get thicker
        ObjectHandle h;
        h.kind = ObjectHandle::Kind::WALL;
        auto shell = find_hollow(doc, body);
        const double t = shell ? shell->m_thickness : 0;
        h.face = edge - dx * t;
        h.dir = -dx;
        h.pos = h.face - dx * out * .55; // on the top face, a little in from its edge
        h.label = "Wall thickness";
        handles.push_back(h);
    }
    {
        // dragged in towards the middle of the object, the edges get rounder
        ObjectHandle h;
        h.kind = ObjectHandle::Kind::ROUNDING;
        h.face = edge;
        h.dir = glm::normalize(-dx - az);
        h.pos = edge + glm::normalize(dx + az) * out * .3; // on the corner where the top meets the side
        h.label = "Round edges";
        handles.push_back(h);
    }
    return handles;
}

// where along the line (from `face`, along `dir`) the pointer is, in mm from `face`
double ray_param_along_line(const glm::dvec3 &face, const glm::dvec3 &dir,
                            const std::pair<glm::dvec3, glm::dvec3> &ray)
{
    const auto &[o, d] = ray;
    const auto u = dir;
    const auto w0 = face - o;
    const double a = glm::dot(u, u), b = glm::dot(u, d), c = glm::dot(d, d), dd = glm::dot(u, w0),
                 e = glm::dot(d, w0);
    const double denom = a * c - b * b;
    if (std::abs(denom) < 1e-12)
        return 0; // looking straight along it
    return (b * e - c * dd) / denom;
}

// where along the handle's line the pointer is, in mm from the face
static double param_along(const Editor::ObjectHandle &h, const std::pair<glm::dvec3, glm::dvec3> &ray)
{
    return ray_param_along_line(h.face, h.dir, ray);
}

std::optional<Editor::ObjectHandle> Editor::get_handle_under_cursor()
{
    const auto cursor = glm::dvec2(get_canvas().get_cursor_pos_win());
    std::optional<ObjectHandle> best;
    double best_d = 14; // px
    for (const auto &h : get_object_handles()) {
        const auto d = glm::length(get_canvas().project_to_window(h.pos) - cursor);
        if (d < best_d) {
            best_d = d;
            best = h;
        }
    }
    return best;
}

// the direction (radians, about the upright line through `pivot`) the pointer's ray points at
static double pointer_angle(const std::pair<glm::dvec3, glm::dvec3> &ray, const glm::dvec3 &pivot)
{
    const auto &[o, d] = ray;
    if (std::abs(d.z) < 1e-9)
        return 0;
    const auto p = o + d * ((pivot.z - o.z) / d.z);
    return std::atan2(p.y - pivot.y, p.x - pivot.x);
}

void Editor::turn_object(std::vector<PartPosition> &cache, const std::vector<UUID> &parts, const glm::dvec3 &pivot,
                         double radians)
{
    auto &doc = m_core.get_current_document();
    if (cache.empty()) {
        // shapes on a shared plane get one of their own first, so they can turn
        for (const auto &p : parts) {
            auto leaves = get_object_parts(doc, p);
            if (leaves.empty())
                leaves.push_back(p); // a single shape
            for (const auto &part : leaves) {
                give_own_workplane(doc, part);
                if (auto pos = get_part_position(doc, part))
                    cache.push_back(*pos);
            }
        }
    }
    for (const auto &pos : cache)
        set_part_angle(doc, pos, pivot, radians);
}

bool Editor::begin_handle_drag()
{
    m_handle_drag.reset();
    auto h = get_handle_under_cursor();
    if (!h)
        return false;
    auto &doc = m_core.get_current_document();
    HandleDrag drag;
    drag.handle = *h;
    const bool part = m_object_editor_part && doc.get_groups().contains(m_object_editor_part);
    const auto body = part ? m_object_editor_part : m_object_editor_body;
    // a part changes with the ones like it
    drag.parts = part ? get_part_set(doc, m_object_editor_body, body) : std::vector<UUID>{body};
    switch (h->kind) {
    case ObjectHandle::Kind::DIMENSION: {
        auto datum = dynamic_cast<IConstraintDatum *>(doc.m_constraints.at(h->constraint).get());
        if (!datum)
            return false;
        drag.start_value = datum->get_datum() * h->scale;
        if (part) {
            drag.follow = begin_parts_follow(doc, get_object_parts(doc, m_object_editor_body), drag.parts);
            const auto &dims = drag.follow->dims;
            auto it = std::ranges::find_if(dims, [&h](const auto &d) { return d.constraint == h->constraint; });
            if (it == dims.end())
                drag.follow.reset();
            else
                drag.dim_index = it - dims.begin();
        }
    } break;
    case ObjectHandle::Kind::WALL: {
        auto shell = find_hollow(doc, body);
        drag.start_value = shell ? shell->m_thickness : 0;
        drag.open = shell ? shell->m_open : GroupShell::Open::TOP;
    } break;
    case ObjectHandle::Kind::ROUNDING: {
        auto fillet = find_round_edges(doc, body);
        drag.start_value = fillet ? fillet->m_radius : 0;
    } break;
    case ObjectHandle::Kind::LIFT: {
        const auto bounds = get_body_bounds(doc, body);
        if (!bounds)
            return false;
        drag.lift_start = bounds->lo.z;
    } break;
    case ObjectHandle::Kind::ROTATE: {
        const auto bounds = get_body_bounds(doc, body);
        if (!bounds)
            return false;
        drag.pivot = (bounds->lo + bounds->hi) / 2.;
        drag.start_angle = pointer_angle(get_canvas().get_cursor_ray(), drag.pivot);
        drag.handle.face = drag.pivot;
    } break;
    }
    drag.start_param = param_along(*h, get_canvas().get_cursor_ray());
    m_handle_drag = drag;
    get_canvas().inhibit_drag_selection();
    m_win.set_key_hint_label_text(h->label);
    return true;
}

void Editor::update_handle_drag()
{
    if (!m_handle_drag)
        return;
    auto &drag = *m_handle_drag;
    auto &doc = m_core.get_current_document();
    const double moved = param_along(drag.handle, get_canvas().get_cursor_ray()) - drag.start_param;
    double value = drag.start_value + moved * drag.handle.per_mm;

    if (drag.handle.kind == ObjectHandle::Kind::LIFT) {
        // whole millimetres, and not below the floor it started on
        const double height = std::max(0., std::round(drag.lift_start + moved));
        const double delta = height - drag.lift_start;
        if (!drag.moved && delta == 0)
            return;
        if (drag.moved && height == drag.last_value)
            return;
        if (drag.lift_parts.empty()) {
            // shapes on a shared plane get one of their own first, so they can go up and down
            for (const auto &p : drag.parts) {
                auto leaves = get_object_parts(doc, p);
                if (leaves.empty())
                    leaves.push_back(p); // a single shape
                for (const auto &part : leaves) {
                    give_own_workplane(doc, part);
                    if (auto pos = get_part_position(doc, part))
                        drag.lift_parts.push_back(*pos);
                }
            }
            if (drag.lift_parts.empty())
                return;
        }
        for (const auto &pos : drag.lift_parts)
            set_part_position(doc, pos, {0, 0, delta});
        drag.last_value = height;
        drag.moved = true;
        char buf[64];
        snprintf(buf, sizeof(buf), "Height above the floor: %.0f mm", height);
        m_win.set_key_hint_label_text(buf);
        doc.update_pending({}, {}, true);
        canvas_update_keep_selection();
        schedule_deferred_solid_models();
        return;
    }

    if (drag.handle.kind == ObjectHandle::Kind::ROTATE) {
        // from where the pointer began, whole degrees, and a click into place at the quarter turns' halves
        double delta = pointer_angle(get_canvas().get_cursor_ray(), drag.pivot) - drag.start_angle;
        delta = std::remainder(delta, glm::two_pi<double>());
        double degrees = std::round(glm::degrees(delta));
        const double snapped = std::round(degrees / 15) * 15;
        if (std::abs(degrees - snapped) <= 3)
            degrees = snapped;
        if (!drag.moved && degrees == 0)
            return;
        if (drag.moved && degrees == drag.last_value)
            return;
        turn_object(drag.turn_parts, drag.parts, drag.pivot, glm::radians(degrees));
        if (drag.turn_parts.empty())
            return;
        drag.last_value = degrees;
        drag.moved = true;
        char buf[64];
        snprintf(buf, sizeof(buf), "Turned by %.0f degrees", degrees);
        m_win.set_key_hint_label_text(buf);
        doc.update_pending({}, {}, true);
        canvas_update_keep_selection();
        schedule_deferred_solid_models();
        return;
    }

    if (drag.handle.kind == ObjectHandle::Kind::DIMENSION) {
        if (!doc.m_constraints.contains(drag.handle.constraint)) {
            m_handle_drag.reset();
            return;
        }
        auto datum = dynamic_cast<IConstraintDatum *>(doc.m_constraints.at(drag.handle.constraint).get());
        // whole millimetres, half millimetres for small things
        const double step = drag.start_value < 10 ? .5 : 1;
        value = std::max(step, std::round(value / step) * step);
        const auto [lo, hi] = datum->get_datum_range();
        value = std::clamp(value, lo * drag.handle.scale, hi * drag.handle.scale);
        if (value / drag.handle.scale == datum->get_datum())
            return;
        if (drag.follow) {
            set_parts_dimension(doc, *drag.follow, drag.dim_index, value);
        }
        else {
            datum->set_datum(value / drag.handle.scale);
            doc.set_group_solve_pending(drag.handle.group);
        }
    }
    else {
        // tenths of a millimetre; back to nothing at the start
        value = std::max(0., std::round(value * 10) / 10);
        if (drag.handle.kind == ObjectHandle::Kind::ROUNDING) {
            // two roundings meet on a wall's rim: each can take a bit less than half of it
            value = std::min(value, get_max_round_edges(doc, drag.parts.front()));
        }
        if (value == drag.last_value)
            return;
        for (const auto &p : drag.parts) {
            if (drag.handle.kind == ObjectHandle::Kind::WALL)
                set_hollow(doc, p, value, drag.open);
            else
                set_round_edges(doc, p, value);
        }
        // new steps show only once the view goes up to them
        get_current_document_view().m_current_group = doc.get_groups_sorted().back()->m_uuid;
        m_core.set_current_group(doc.get_groups_sorted().back()->m_uuid);
    }
    drag.last_value = value;
    drag.moved = true;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s: %.1f mm", drag.handle.label.c_str(), value);
    m_win.set_key_hint_label_text(buf);
    // a size waits for slow steps after it; a wall or rounding is what the drag is about, so it's built
    const bool interactive =
            drag.handle.kind != ObjectHandle::Kind::WALL && drag.handle.kind != ObjectHandle::Kind::ROUNDING;
    doc.update_pending({}, {}, interactive);
    canvas_update_keep_selection();
    schedule_deferred_solid_models();
}

void Editor::end_handle_drag()
{
    if (!m_handle_drag)
        return;
    const auto drag = *m_handle_drag;
    m_handle_drag.reset();
    reset_key_hint_label();
    if (!drag.moved)
        return;
    // too round or walls too thick for it: back to how it was, rather than a broken object
    auto &doc = m_core.get_current_document();
    bool undone = false;
    for (const auto &p : drag.parts) {
        if (drag.handle.kind == ObjectHandle::Kind::ROUNDING) {
            if (auto f = find_round_edges(doc, p); f && !f->get_solid_model()) {
                try_round_edges(doc, p, drag.start_value, 0);
                undone = true;
            }
        }
        else if (drag.handle.kind == ObjectHandle::Kind::WALL) {
            if (auto h = find_hollow(doc, p); h && !h->get_solid_model()) {
                try_hollow(doc, p, drag.start_value, drag.open, 0);
                undone = true;
            }
        }
    }
    if (undone)
        m_workspace_browser->show_toast(drag.handle.kind == ObjectHandle::Kind::ROUNDING
                                                ? "That was rounder than this object can be"
                                                : "Walls that thick don't fit this object");
    m_core.set_needs_save();
    m_core.rebuild("change " + drag.handle.label); // the object panel refreshes on rebuild
    m_workspace_browser->update_documents(get_current_document_views());
    canvas_update_keep_selection();
}

// what a handle does, shown while the pointer is over it
static std::string get_handle_hint(const Editor::ObjectHandle &h)
{
    using Kind = Editor::ObjectHandle::Kind;
    switch (h.kind) {
    case Kind::LIFT:
        return "Lift: drag up or down to raise the object off the floor";
    case Kind::WALL:
        return "Wall thickness: drag inwards to hollow the object out, back to the edge for a solid one";
    case Kind::ROUNDING:
        return "Round edges: drag in towards the middle of the object";
    case Kind::ROTATE:
        return "Rotate: drag round the object to turn it (it snaps every 15 degrees)";
    case Kind::DIMENSION:
        break;
    }
    if (h.label == "Height")
        return "Height: drag up or down";
    if (h.label == "Width")
        return "Width: drag sideways (the object stays centred)";
    if (h.label == "Depth")
        return "Depth: drag towards or away from you (the object stays centred)";
    if (h.label == "Diameter" || h.label == "Outer diameter")
        return h.label + ": drag outwards to make it wider";
    if (h.label == "Radius")
        return "Radius: drag outwards to make it wider";
    if (h.label == "Inner diameter")
        return "Inner diameter: drag outwards to widen the hole";
    return "Drag to change: " + h.label;
}

// how to move what the pointer is over: a light, or the picked object
std::string Editor::get_move_hint()
{
    auto &doc = m_core.get_current_document();
    const auto cursor = glm::dvec2(get_canvas().get_cursor_pos_win());
    auto close_to = [this, &cursor](const glm::dvec3 &p) {
        return glm::length(get_canvas().project_to_window(p) - cursor) < 14;
    };
    for (const auto &light : doc.m_lights) {
        const bool aimed = light.m_type != Light::Type::POINT;
        if (close_to(light.get_height_handle(false)) || (aimed && close_to(light.get_height_handle(true))))
            return "Drag up or down to change the height of the light";
        if (close_to(light.m_position))
            return "Drag to move the light (Shift: one direction); the dot below it changes its height";
        if (aimed && close_to(light.m_target))
            return "Drag to aim the light (Shift: one direction)";
    }
    if (m_object_editor && !m_object_editor_light && m_object_editor_body && get_canvas().is_face_under_cursor()
        && can_move_object(doc, m_object_editor_body)) {
        const auto [origin, dir] = get_canvas().get_cursor_ray();
        const auto body = pick_body(doc, m_core.get_current_group(), origin, dir,
                                    [this](const UUID &b) { return body_is_shown(b); });
        if (body && *body == m_object_editor_body)
            return "Drag to move (Shift: one direction); drag the Lift dot up or down to raise it";
    }
    return {};
}

// what's under the pointer, shown at the bottom
void Editor::update_handle_hint()
{
    if (m_handle_drag)
        return;
    const auto h = get_handle_under_cursor();
    std::string label = h ? get_handle_hint(*h) : std::string();
    if (label.empty() && !m_core.tool_is_active() && m_core.has_documents())
        label = get_move_hint();
    if (label == m_handle_hint)
        return;
    m_handle_hint = label;
    if (label.size())
        m_win.set_key_hint_label_text(label);
    else
        reset_key_hint_label();
}

} // namespace dune3d

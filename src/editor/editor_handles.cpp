#include "editor.hpp"
#include "object_dimensions.hpp"
#include "canvas/canvas.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/body_solids.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint.hpp"
#include "document/constraint/iconstraint_datum.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include <algorithm>
#include <limits>

namespace dune3d {

/*
 * Handles on the picked object, for resizing it by dragging: one on top for
 * the height, one on the right for the width (or diameter, radius) and one at
 * the back for the depth. They change the same dimensions as the object panel;
 * the face under the handle follows the pointer.
 */

std::vector<Editor::ObjectHandle> Editor::get_object_handles()
{
    std::vector<ObjectHandle> handles;
    if (!m_object_editor || m_object_editor_light || !m_object_editor_body || m_core.tool_is_active())
        return handles;
    if (!m_core.has_documents() || m_core.get_current_idocument_info().get_uuid() != m_object_editor_doc)
        return handles;
    auto &doc = m_core.get_current_idocument_info().get_document();
    if (!doc.get_groups().contains(m_object_editor_body))
        return handles;
    const auto dims = get_object_dimensions(doc, m_object_editor_body);
    auto find = [&dims](const char *label) -> const ObjectDimension * {
        for (const auto &d : dims) {
            if (d.label == label)
                return &d;
        }
        return nullptr;
    };

    // the frame the object stands in: its sketch's workplane
    const auto &body_group = doc.get_group(m_object_editor_body);
    auto wrkpl = doc.get_entity_ptr<EntityWorkplane>(body_group.m_active_wrkpl);
    if (!wrkpl)
        return handles;
    const auto origin = wrkpl->m_origin;
    const auto ax = wrkpl->transform_relative({1, 0});
    const auto ay = wrkpl->transform_relative({0, 1});
    const auto az = wrkpl->get_normal_vector();

    const SolidModelOcc *solid = nullptr;
    for (const auto &bs : get_body_solids(doc, m_core.get_current_group(),
                                          [this](const UUID &b) { return b == m_object_editor_body; }))
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
    const double size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
    const double out = std::max(size * .15, 2.); // how far the handle sits out from the face
    auto world = [&](const glm::dvec3 &l) { return origin + ax * l.x + ay * l.y + az * l.z; };
    auto add = [&](const ObjectDimension *d, glm::dvec3 face, const glm::dvec3 &dir, double per_mm) {
        if (!d)
            return;
        ObjectHandle h;
        h.face = world(face);
        h.dir = dir;
        h.pos = h.face + dir * out;
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
    const glm::dvec3 side_x{right ? hi.x : lo.x, c.y, c.z};
    const glm::dvec3 side_y{c.x, back ? hi.y : lo.y, c.z};

    // up: the height; a ring (no height) grows by its tube's diameter
    if (auto d = find("Height"))
        add(d, {c.x, c.y, hi.z}, az, 1);
    else if (find("Radius") && find("Diameter"))
        add(find("Diameter"), {c.x, c.y, hi.z}, az, 1);
    // sideways: width, or how wide round things are; they're centred, so the far side moves too
    const auto dx = right ? ax : -ax;
    if (auto d = find("Width"))
        add(d, side_x, dx, 2);
    else if (auto d = find("Outer diameter"))
        add(d, side_x, dx, 2);
    else if (auto d = find("Radius"))
        add(d, side_x, dx, 1);
    else if (auto d = find("Diameter"))
        add(d, side_x, dx, 2);
    // front to back: the depth
    if (auto d = find("Depth"))
        add(d, side_y, back ? ay : -ay, 2);
    return handles;
}

// where along the handle's line the pointer is, in mm from the face
static double param_along(const Editor::ObjectHandle &h, const std::pair<glm::dvec3, glm::dvec3> &ray)
{
    const auto &[o, d] = ray;
    const auto u = h.dir;
    const auto w0 = h.face - o;
    const double a = glm::dot(u, u), b = glm::dot(u, d), c = glm::dot(d, d), dd = glm::dot(u, w0),
                 e = glm::dot(d, w0);
    const double denom = a * c - b * b;
    if (std::abs(denom) < 1e-12)
        return 0; // looking straight along it
    return (b * e - c * dd) / denom;
}

bool Editor::begin_handle_drag()
{
    m_handle_drag.reset();
    const auto cursor = glm::dvec2(get_canvas().get_cursor_pos_win());
    for (const auto &h : get_object_handles()) {
        if (glm::length(get_canvas().project_to_window(h.pos) - cursor) > 14)
            continue;
        auto &doc = m_core.get_current_document();
        auto datum = dynamic_cast<IConstraintDatum *>(doc.m_constraints.at(h.constraint).get());
        if (!datum)
            return false;
        HandleDrag drag;
        drag.handle = h;
        drag.start_value = datum->get_datum() * h.scale;
        drag.start_param = param_along(h, get_canvas().get_cursor_ray());
        m_handle_drag = drag;
        get_canvas().inhibit_drag_selection();
        return true;
    }
    return false;
}

void Editor::update_handle_drag()
{
    if (!m_handle_drag)
        return;
    auto &drag = *m_handle_drag;
    auto &doc = m_core.get_current_document();
    if (!doc.m_constraints.contains(drag.handle.constraint)) {
        m_handle_drag.reset();
        return;
    }
    auto datum = dynamic_cast<IConstraintDatum *>(doc.m_constraints.at(drag.handle.constraint).get());
    const double moved = param_along(drag.handle, get_canvas().get_cursor_ray()) - drag.start_param;
    double value = drag.start_value + moved * drag.handle.per_mm;
    // whole millimetres when the grid's that fine or coarser, half millimetres for small things
    const double step = drag.start_value < 10 ? .5 : 1;
    value = std::max(step, std::round(value / step) * step);
    const auto [lo, hi] = datum->get_datum_range();
    value = std::clamp(value, lo * drag.handle.scale, hi * drag.handle.scale);
    if (value / drag.handle.scale == datum->get_datum())
        return;
    datum->set_datum(value / drag.handle.scale);
    drag.moved = true;
    doc.set_group_solve_pending(drag.handle.group);
    doc.update_pending();
    canvas_update_keep_selection();
}

void Editor::end_handle_drag()
{
    if (!m_handle_drag)
        return;
    const auto drag = *m_handle_drag;
    m_handle_drag.reset();
    if (!drag.moved)
        return;
    m_core.set_needs_save();
    m_core.rebuild("resize " + drag.handle.label); // the object panel refreshes on rebuild
    canvas_update_keep_selection();
}

} // namespace dune3d

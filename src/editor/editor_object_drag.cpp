#include "editor.hpp"
#include "dune3d_appwindow.hpp"
#include "canvas/canvas.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/entity/entity.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_point_distance_hv.hpp"
#include "document/object_parts.hpp"
#include "document/group/group_reference.hpp"
#include "document/body_solids.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include <limits>
#include <ranges>

namespace dune3d {

/*
 * Moving an object by dragging it, like on a work surface. Works for objects
 * whose position is given by horizontal/vertical distances from a workplane
 * origin (the ready-made shapes): dragging changes those two dimensions,
 * snapped to the grid, and the result is one undo step.
 */

std::optional<std::pair<glm::dvec3, double>> Editor::get_move_arrows()
{
    if (!m_object_editor || m_object_editor_light || !m_object_editor_body || m_core.tool_is_active()
        || m_handle_drag)
        return {};
    if (!m_core.has_documents() || m_core.get_current_idocument_info().get_uuid() != m_object_editor_doc)
        return {};
    auto &doc = m_core.get_current_document();
    if (!doc.get_groups().contains(m_object_editor_body) || !body_is_shown(m_object_editor_body)
        || !can_move_object(doc, m_object_editor_body))
        return {};
    glm::dvec3 lo{std::numeric_limits<double>::infinity()}, hi{-std::numeric_limits<double>::infinity()};
    for (const auto &bs : get_body_solids(doc, m_core.get_current_group(),
                                          [this](const UUID &b) { return b == m_object_editor_body; })) {
        for (const auto &face : bs.solid->m_faces) {
            for (const auto &v : face.vertices) {
                lo = glm::min(lo, glm::dvec3(v.x, v.y, v.z));
                hi = glm::max(hi, glm::dvec3(v.x, v.y, v.z));
            }
        }
    }
    if (lo.x > hi.x)
        return {};
    // on the floor under the middle of the object; the arms reach out past its sides
    const auto c = glm::dvec3((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, std::min(lo.z, 0.));
    return std::make_pair(c, std::max(hi.x - lo.x, hi.y - lo.y) / 2 + std::max(hi.x - lo.x, hi.y - lo.y) * .35 + 8);
}

bool Editor::begin_object_drag(const UUID &doc_uu, const UUID &body_uu)
{
    m_object_drag.reset();
    auto &doc = m_core.get_idocument_info(doc_uu).get_document();
    const auto body_groups = doc.get_groups_by_body();
    auto it = std::ranges::find_if(body_groups, [&body_uu](const auto &bg) { return bg.get_group().m_uuid == body_uu; });
    if (it == body_groups.end())
        return false;

    ObjectDrag drag;
    drag.doc = doc_uu;
    drag.body = body_uu;
    for (auto gr : it->groups) {
        for (auto &[uu, co] : doc.m_constraints) {
            if (co->m_group != gr->m_uuid)
                continue;
            auto hv = dynamic_cast<const ConstraintPointDistanceHV *>(co.get());
            if (!hv || hv->is_measurement())
                continue;
            auto en = doc.get_entity_ptr(hv->m_entity1.entity);
            if (!en || en->get_type() != Entity::Type::WORKPLANE)
                continue;
            if (drag.group && drag.group != gr->m_uuid)
                continue; // only the first group with a position
            drag.group = gr->m_uuid;
            drag.wrkpl = hv->m_wrkpl;
            if (co->get_type() == Constraint::Type::POINT_DISTANCE_HORIZONTAL)
                drag.cx = uu;
            else
                drag.cy = uu;
        }
    }
    if (!drag.cx || !drag.cy) {
        // a joined object: all of its parts move, on the floor
        drag.cx = drag.cy = UUID();
        for (const auto &p : get_object_parts(doc, body_uu)) {
            auto pos = get_part_position(doc, p);
            if (!pos)
                return false;
            drag.parts.push_back(*pos);
        }
        if (drag.parts.empty())
            return false;
        drag.wrkpl = doc.get_reference_group().get_workplane_xy_uuid();
    }
    if (!doc.get_entity_ptr<EntityWorkplane>(drag.wrkpl))
        return false;

    auto &wrkpl = doc.get_entity<EntityWorkplane>(drag.wrkpl);
    drag.start = wrkpl.project(get_canvas().get_cursor_pos_for_plane(wrkpl.m_origin, wrkpl.get_normal_vector()));
    if (drag.parts.size()) {
        drag.x0 = drag.y0 = 0; // how far they've moved
    }
    else {
        drag.x0 = dynamic_cast<const ConstraintPointDistanceHV &>(*doc.m_constraints.at(drag.cx)).m_distance;
        drag.y0 = dynamic_cast<const ConstraintPointDistanceHV &>(*doc.m_constraints.at(drag.cy)).m_distance;
    }
    m_object_drag = drag;
    get_canvas().inhibit_drag_selection();
    return true;
}

void Editor::update_object_drag()
{
    if (!m_object_drag)
        return;
    auto &drag = *m_object_drag;
    auto &doc = m_core.get_idocument_info(drag.doc).get_document();
    if (drag.parts.empty() && (!doc.m_constraints.contains(drag.cx) || !doc.m_constraints.contains(drag.cy))) {
        m_object_drag.reset();
        return;
    }
    auto &wrkpl = doc.get_entity<EntityWorkplane>(drag.wrkpl);
    const auto p = wrkpl.project(get_canvas().get_cursor_pos_for_plane(wrkpl.m_origin, wrkpl.get_normal_vector()));
    auto delta = p - drag.start;
    // Shift: only along the axis the pointer has moved furthest on
    if (get_canvas().is_shift_down()) {
        if (std::abs(delta.x) >= std::abs(delta.y))
            delta.y = 0;
        else
            delta.x = 0;
    }
    glm::dvec2 pos = glm::dvec2(drag.x0, drag.y0) + delta;
    if (const auto step = get_grid_snap(wrkpl); step > 0)
        pos = glm::round(pos / step) * step;
    if (!drag.moved && glm::length(p - drag.start) < 1e-9)
        return;
    if (drag.parts.size()) {
        if (drag.moved && pos == drag.last)
            return;
        drag.last = pos;
        for (const auto &part : drag.parts)
            set_part_position(doc, part, wrkpl.transform_relative(pos));
    }
    else {
        auto &cx = dynamic_cast<ConstraintPointDistanceHV &>(*doc.m_constraints.at(drag.cx));
        auto &cy = dynamic_cast<ConstraintPointDistanceHV &>(*doc.m_constraints.at(drag.cy));
        if (cx.m_distance == pos.x && cy.m_distance == pos.y)
            return;
        cx.m_distance = pos.x;
        cy.m_distance = pos.y;
        doc.set_group_solve_pending(drag.group);
    }
    drag.moved = true;
    char buf[64];
    snprintf(buf, sizeof(buf), "Moving: X %+.1f mm, Y %+.1f mm (from the start)", pos.x - drag.x0, pos.y - drag.y0);
    m_win.set_key_hint_label_text(buf);
    doc.update_pending({}, {}, true);
    canvas_update_keep_selection();
    schedule_deferred_solid_models();
}

void Editor::schedule_deferred_solid_models()
{
    m_deferred_solid_models_connection.disconnect();
    if (!m_core.has_documents() || !m_core.get_current_document().has_deferred_solid_models())
        return;
    m_deferred_solid_models_connection = Glib::signal_timeout().connect(
            [this] {
                if (!m_core.has_documents())
                    return false;
                auto &doc = m_core.get_current_document();
                if (!doc.has_deferred_solid_models())
                    return false;
                // a tool only updates up to the current group, like Core::solve_current
                if (m_core.tool_is_active()) {
                    doc.update_pending(m_core.get_current_group());
                    canvas_update_from_tool();
                }
                else {
                    doc.update_pending();
                    canvas_update_keep_selection();
                }
                return false;
            },
            150);
}

void Editor::end_object_drag()
{
    if (!m_object_drag)
        return;
    const auto drag = *m_object_drag;
    m_object_drag.reset();
    reset_key_hint_label();
    if (!drag.moved)
        return;
    m_core.set_needs_save();
    m_core.rebuild("move object"); // the object panel refreshes on rebuild
    canvas_update_keep_selection();
}

} // namespace dune3d

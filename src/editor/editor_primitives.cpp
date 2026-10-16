#include "editor.hpp"
#include "workspace_browser.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/body_solids.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include "canvas/canvas.hpp"
#include "document/primitives.hpp"
#include "document/group/group_sketch.hpp"
#include "document/group/group_reference.hpp"
#include "document/group/group_extrude.hpp"
#include "document/entity/entity_text.hpp"
#include "document/entity/entity_workplane.hpp"
#include "util/text_render.hpp"
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

namespace dune3d {

void Editor::add_primitive(Primitive primitive, bool as_hole)
{
    if (m_core.tool_is_active() || !m_core.has_documents())
        return;
    CanvasUpdater canvas_updater{*this};
    auto &doc = m_core.get_current_document();
    // next to what's there rather than inside it
    const bool first = get_body_solids(doc, m_core.get_current_group()).empty();
    const auto pos = find_free_spot(doc, m_core.get_current_group());
    PrimitiveParams params;
    if (as_hole) {
        // a 6 mm drill, long enough to go through most things
        params.diameter = 6;
        params.height = 30;
    }
    auto &group = dune3d::add_primitive(doc, m_core.get_current_group(), primitive, pos, params);
    if (as_hole) {
        auto &body = *doc.get_group(group.find_body(doc).group.m_uuid).m_body;
        body.m_hole = true;
        body.m_name = "Hole";
    }
    m_core.set_needs_save();
    m_core.rebuild("add " + group.m_name);
    m_workspace_browser->update_documents(get_current_document_views());
    m_workspace_browser->select_group(group.m_uuid);
    zoom_to_fit(!first); // the first one gets framed so it's big enough to work with
}

/*
 * 3D text: a text entity in a sketch, extruded, in a new body. Its text and
 * size are edited from the object panel; the rest works like the other shapes
 * (X/Y position from the workplane origin, height of the extrusion).
 */
void Editor::add_text_object()
{
    if (m_core.tool_is_active() || !m_core.has_documents())
        return;
    CanvasUpdater canvas_updater{*this};
    auto &doc = m_core.get_current_document();
    auto &current = doc.get_group(m_core.get_current_group());
    const UUID wrkpl =
            current.m_active_wrkpl ? current.m_active_wrkpl : doc.get_reference_group().get_workplane_xy_uuid();
    const auto pos = find_free_spot(doc, current.m_uuid, 40);

    auto &sketch = doc.insert_group<GroupSketch>(UUID::random(), current.m_uuid);
    sketch.m_name = doc.find_next_group_name(Group::Type::SKETCH);
    sketch.m_active_wrkpl = wrkpl;
    sketch.m_body.emplace();
    sketch.m_body->m_name = "Text";

    auto &text = doc.add_entity<EntityText>(UUID::random());
    text.m_group = sketch.m_uuid;
    text.m_wrkpl = wrkpl;
    text.m_text = "Text";
    text.m_lock_angle = true;
    text.m_lock_scale = true;
    render_text(text, get_pango_context(), doc);
    {
        // 10 mm tall, centred on the free spot
        const auto [lo, hi] = text.get_bbox();
        if (hi.y > lo.y)
            text.m_scale *= 10 / (hi.y - lo.y);
        const auto [lo2, hi2] = text.get_bbox();
        text.m_origin += pos - (lo2 + hi2) / 2.;
        text.m_origin = glm::round(text.m_origin * 10.) / 10.; // tidy X/Y
    }
    add_position(doc, sketch.m_uuid, wrkpl, {text.m_uuid, 1}, text.m_origin);
    auto &ex = add_extrusion(doc, sketch.m_uuid, wrkpl, 5);
    ex.m_name = "Text";
    doc.set_group_generate_pending(sketch.m_uuid);

    m_core.set_needs_save();
    m_core.rebuild("add text");
    m_workspace_browser->update_documents(get_current_document_views());
    m_workspace_browser->select_group(ex.m_uuid);
    zoom_to_fit(true);
}

EntityText *Editor::find_text_entity(Document &doc, const UUID &body)
{
    for (const auto &bg : doc.get_groups_by_body()) {
        if (bg.get_group().m_uuid != body)
            continue;
        for (auto gr : bg.groups) {
            for (auto &[uu, en] : doc.m_entities) {
                if (en->m_group != gr->m_uuid || en->m_kind != ItemKind::USER)
                    continue;
                if (auto t = dynamic_cast<EntityText *>(en.get()))
                    return t;
            }
        }
    }
    return nullptr;
}

void Editor::zoom_to_fit(bool only_if_needed)
{
    if (!m_core.has_documents())
        return;
    auto &doc = m_core.get_current_document();
    Bnd_Box box;
    for (const auto &bs :
         get_body_solids(doc, m_core.get_current_group(), [this](const UUID &b) { return body_is_shown(b); }))
        BRepBndLib::Add(bs.solid->m_shape_acc, box, true); // by the triangles: meshes have no surfaces
    for (const auto &light : doc.m_lights) {
        if (light.m_type != Light::Type::SUN) // that's far away anyway
            box.Add(gp_Pnt(light.m_position.x, light.m_position.y, light.m_position.z));
    }
    glm::vec3 lo{-100, -100, 0}, hi{100, 100, 0}; // the XY plane
    if (!box.IsVoid()) {
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        lo = {x0, y0, z0};
        hi = {x1, y1, z1};
    }
    if (only_if_needed) {
        bool all_visible = true;
        for (int i = 0; i < 8; i++) {
            const glm::vec3 corner{i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z};
            all_visible = all_visible && get_canvas().is_in_view(corner);
        }
        if (all_visible)
            return;
    }
    get_canvas().zoom_to_fit((lo + hi) / 2.f, std::max(glm::length(hi - lo) / 2, 5.f));
}

} // namespace dune3d

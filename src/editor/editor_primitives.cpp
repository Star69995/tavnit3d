#include "editor.hpp"
#include "workspace_browser.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/body_solids.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include "canvas/canvas.hpp"
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

namespace dune3d {

void Editor::add_primitive(Primitive primitive)
{
    if (m_core.tool_is_active() || !m_core.has_documents())
        return;
    CanvasUpdater canvas_updater{*this};
    auto &doc = m_core.get_current_document();
    // next to what's there rather than inside it
    const auto pos = find_free_spot(doc, m_core.get_current_group());
    auto &group = dune3d::add_primitive(doc, m_core.get_current_group(), primitive, pos);
    m_core.set_needs_save();
    m_core.rebuild("add " + group.m_name);
    m_workspace_browser->update_documents(get_current_document_views());
    m_workspace_browser->select_group(group.m_uuid);
    zoom_to_fit(true);
}

void Editor::zoom_to_fit(bool only_if_needed)
{
    if (!m_core.has_documents())
        return;
    auto &doc = m_core.get_current_document();
    Bnd_Box box;
    for (const auto &bs :
         get_body_solids(doc, m_core.get_current_group(), [this](const UUID &b) { return body_is_shown(b); }))
        BRepBndLib::AddOptimal(bs.solid->m_shape_acc, box, false, false);
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

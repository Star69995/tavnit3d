#include "editor.hpp"
#include "workspace_browser.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"

namespace dune3d {

void Editor::add_primitive(Primitive primitive)
{
    if (m_core.tool_is_active() || !m_core.has_documents())
        return;
    CanvasUpdater canvas_updater{*this};
    auto &doc = m_core.get_current_document();
    auto &group = dune3d::add_primitive(doc, m_core.get_current_group(), primitive);
    m_core.set_needs_save();
    m_core.rebuild("add " + group.m_name);
    m_workspace_browser->update_documents(get_current_document_views());
    m_workspace_browser->select_group(group.m_uuid);
}

} // namespace dune3d

#include "tool_sketch_on_face.hpp"
#include "document/document.hpp"
#include "document/face_sketch.hpp"
#include "document/group/group.hpp"
#include "editor/editor_interface.hpp"
#include "util/action_label.hpp"
#include "tool_common_impl.hpp"

namespace dune3d {

ToolResponse ToolSketchOnFace::begin(const ToolArgs &args)
{
    std::vector<ActionLabelInfo> actions;
    actions.emplace_back(InToolActionID::LMB, "pick face");
    actions.emplace_back(InToolActionID::RMB, "cancel");
    m_intf.tool_bar_set_actions(actions);
    m_intf.tool_bar_set_tool_tip("Click a flat face to sketch on it");
    return ToolResponse();
}

ToolResponse ToolSketchOnFace::update(const ToolArgs &args)
{
    if (args.type != ToolEventType::ACTION)
        return ToolResponse();

    switch (args.action) {
    case InToolActionID::LMB: {
        const auto [origin, dir] = m_intf.get_cursor_ray();
        auto face = pick_planar_face(get_doc(), m_core.get_current_group(), origin, dir,
                                     [this](const UUID &body) { return m_intf.body_is_shown(body); });
        if (!face) {
            m_intf.tool_bar_flash("No flat face there");
            return ToolResponse();
        }
        auto &sketch = add_sketch_on_face(get_doc(), m_core.get_current_group(), *face);
        return ToolResponse::commit_and_set_current_group(sketch.m_uuid);
    }

    case InToolActionID::RMB:
    case InToolActionID::CANCEL:
        return ToolResponse::end();

    default:;
    }
    return ToolResponse();
}
} // namespace dune3d

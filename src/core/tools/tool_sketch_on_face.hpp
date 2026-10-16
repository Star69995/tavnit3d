#pragma once
#include "tool_common.hpp"
#include "in_tool_action/in_tool_action.hpp"

namespace dune3d {

// Click a flat face of a solid to start a new sketch on it.
class ToolSketchOnFace : public ToolCommon {
public:
    using ToolCommon::ToolCommon;

    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    bool is_specific() override
    {
        return false;
    }

    std::set<InToolActionID> get_actions() const override
    {
        using I = InToolActionID;
        return {
                I::LMB,
                I::CANCEL,
                I::RMB,
        };
    }
};
} // namespace dune3d

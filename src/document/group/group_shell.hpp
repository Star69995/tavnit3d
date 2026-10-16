#pragma once
#include "group.hpp"
#include "igroup_solid_model.hpp"

namespace dune3d {

class SolidModel;

// Hollows out the body's solid so far: walls of the given thickness, inwards,
// open on one side (or closed all round). Which face is the open one is worked
// out again whenever the solid changes, so resizing the object keeps it right.
class GroupShell : public Group, public IGroupSolidModel {
public:
    explicit GroupShell(const UUID &uu);
    explicit GroupShell(const UUID &uu, const json &j);
    static constexpr Type s_type = Type::SHELL;
    Type get_type() const override
    {
        return s_type;
    }

    double m_thickness = 2;
    enum class Open { TOP, BOTTOM, NONE };
    Open m_open = Open::TOP;

    Operation m_operation = Operation::UNION;
    Operation get_operation() const override
    {
        return m_operation;
    }
    void set_operation(Operation op) override
    {
    }

    json serialize() const override;
    std::unique_ptr<Group> clone() const override;

    std::list<GroupStatusMessage> m_shell_messages;
    std::list<GroupStatusMessage> get_messages() const override;

    std::shared_ptr<const SolidModel> m_solid_model;
    const SolidModel *get_solid_model() const override;
    void update_solid_model(const Document &doc) override;
};

} // namespace dune3d

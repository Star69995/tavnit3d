#pragma once
#include "group.hpp"
#include "igroup_solid_model.hpp"

namespace dune3d {

class SolidModel;

// A helical ridge (a real screw thread) winding round the body's solid so far,
// which is a cylinder of the thread's root radius standing upright: the ridge
// is `m_depth` high above it, `m_pitch` apart from turn to turn, from the
// bottom of the solid up `m_length`, with the first stretch tapered to a
// chamfered tip. The axis is the middle of the solid, so it follows moves.
class GroupThread : public Group, public IGroupSolidModel {
public:
    explicit GroupThread(const UUID &uu);
    explicit GroupThread(const UUID &uu, const json &j);
    static constexpr Type s_type = Type::THREAD;
    Type get_type() const override
    {
        return s_type;
    }

    double m_root_radius = 3.2;
    double m_pitch = 1.25;
    double m_depth = .77;
    double m_length = 30; // along the axis from the bottom; shortened to fit the solid

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

    std::list<GroupStatusMessage> m_thread_messages;
    std::list<GroupStatusMessage> get_messages() const override;

    std::shared_ptr<const SolidModel> m_solid_model;
    const SolidModel *get_solid_model() const override;
    void update_solid_model(const Document &doc) override;
};

} // namespace dune3d

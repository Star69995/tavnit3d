#include "group_thread.hpp"
#include "nlohmann/json.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

GroupThread::GroupThread(const UUID &uu) : Group(uu)
{
}

GroupThread::GroupThread(const UUID &uu, const json &j)
    : Group(uu, j), m_root_radius(j.at("root_radius").get<double>()), m_pitch(j.at("pitch").get<double>()),
      m_depth(j.at("depth").get<double>()), m_length(j.at("length").get<double>())
{
}

json GroupThread::serialize() const
{
    auto j = Group::serialize();
    j["root_radius"] = m_root_radius;
    j["pitch"] = m_pitch;
    j["depth"] = m_depth;
    j["length"] = m_length;
    return j;
}

std::unique_ptr<Group> GroupThread::clone() const
{
    return std::make_unique<GroupThread>(*this);
}

std::list<GroupStatusMessage> GroupThread::get_messages() const
{
    auto msg = Group::get_messages();
    msg.insert(msg.end(), m_thread_messages.begin(), m_thread_messages.end());
    return msg;
}

const SolidModel *GroupThread::get_solid_model() const
{
    return m_solid_model.get();
}

void GroupThread::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

} // namespace dune3d

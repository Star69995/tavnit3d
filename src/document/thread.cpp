#include "thread.hpp"
#include "document/document.hpp"
#include "document/solid_model/solid_model.hpp"
#include "combine.hpp"

namespace dune3d {

static std::vector<const Group *> body_groups_of(Document &doc, const UUID &body)
{
    for (const auto &bg : doc.get_groups_by_body()) {
        if (bg.get_group().m_uuid == body)
            return bg.groups;
    }
    return {};
}

GroupThread *find_thread(Document &doc, const UUID &body)
{
    const auto groups = body_groups_of(doc, body);
    if (groups.empty() || groups.back()->get_type() != Group::Type::THREAD)
        return nullptr;
    return &doc.get_group<GroupThread>(groups.back()->m_uuid);
}

GroupThread *set_thread(Document &doc, const UUID &body, double root_radius, double pitch, double depth, double length)
{
    auto thread = find_thread(doc, body);
    if (!thread) {
        const auto groups = body_groups_of(doc, body);
        if (groups.empty())
            return nullptr;
        const auto &last = *groups.back();
        if (!SolidModel::get_last_solid_model(doc, last, SolidModel::IncludeGroup::YES))
            return nullptr;
        thread = &doc.insert_group<GroupThread>(UUID::random(), last.m_uuid);
        thread->m_name = "Thread";
        repoint_combinations(doc, last.m_uuid, thread->m_uuid);
    }
    thread->m_root_radius = root_radius;
    thread->m_pitch = pitch;
    thread->m_depth = depth;
    thread->m_length = length;
    doc.set_group_generate_pending(thread->m_uuid);
    return thread;
}

} // namespace dune3d

#include "combine.hpp"
#include "document/document.hpp"
#include "document/group/group_solid_model_operation.hpp"
#include <ranges>

namespace dune3d {

namespace {
const Group &last_group_of_body(const Document &doc, const UUID &body)
{
    const auto body_groups = doc.get_groups_by_body();
    auto it = std::ranges::find_if(body_groups, [&body](const auto &bg) { return bg.get_group().m_uuid == body; });
    if (it == body_groups.end())
        throw std::runtime_error("no such body");
    // the last group of the body that has a solid
    for (auto gr : it->groups | std::views::reverse) {
        if (IGroupSolidModel::try_get_solid_model(*gr))
            return *gr;
    }
    return *it->groups.back();
}
} // namespace

Group &add_combination(Document &doc, const UUID &body_a, const UUID &body_b, IGroupSolidModel::Operation op)
{
    const auto &a = last_group_of_body(doc, body_a);
    const auto &b = last_group_of_body(doc, body_b);
    const auto &name_a = doc.get_group(body_a).m_body->m_name;
    const auto &name_b = doc.get_group(body_b).m_body->m_name;

    auto &group = doc.insert_group<GroupSolidModelOperation>(UUID::random(), doc.get_groups_sorted().back()->m_uuid);
    group.m_source_group_argument = a.m_uuid;
    group.m_source_group_tool = b.m_uuid;
    group.m_operation = op;
    group.m_name = doc.find_next_group_name(Group::Type::SOLID_MODEL_OPERATION);
    group.m_body.emplace();
    switch (op) {
    case IGroupSolidModel::Operation::UNION:
        group.m_body->m_name = name_a + " + " + name_b;
        break;
    case IGroupSolidModel::Operation::DIFFERENCE:
        group.m_body->m_name = name_a + " - " + name_b;
        break;
    case IGroupSolidModel::Operation::INTERSECTION:
        group.m_body->m_name = name_a + " & " + name_b;
        break;
    }
    // keep the look of the first object
    group.m_body->m_color = doc.get_group(body_a).m_body->m_color;
    group.m_body->m_material = doc.get_group(body_a).m_body->m_material;
    doc.set_group_generate_pending(group.m_uuid);
    return group;
}

void repoint_combinations(Document &doc, const UUID &from, const UUID &to)
{
    if (from == to)
        return;
    for (auto gr : doc.get_groups_sorted()) {
        auto op = dynamic_cast<const GroupSolidModelOperation *>(gr);
        if (!op || (op->m_source_group_argument != from && op->m_source_group_tool != from))
            continue;
        auto &g = doc.get_group<GroupSolidModelOperation>(gr->m_uuid);
        if (g.m_source_group_argument == from)
            g.m_source_group_argument = to;
        if (g.m_source_group_tool == from)
            g.m_source_group_tool = to;
        doc.set_group_generate_pending(g.m_uuid);
    }
}

} // namespace dune3d

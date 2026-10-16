#include "hollow.hpp"
#include "document/document.hpp"
#include "document/solid_model/solid_model.hpp"
#include "document/round_edges.hpp"
#include "document/group/group_fillet.hpp"

namespace dune3d {

static constexpr const char *hollow_name = "Hollow";

// the body's groups, without a rounded-edges step at the end: hollowing goes before
// it, so the rim gets rounded too
static std::vector<const Group *> body_groups_of(Document &doc, const UUID &body)
{
    const auto rounded = find_round_edges(doc, body);
    for (const auto &bg : doc.get_groups_by_body()) {
        if (bg.get_group().m_uuid == body) {
            auto groups = bg.groups;
            if (rounded && groups.size() > 1 && groups.back()->m_uuid == rounded->m_uuid)
                groups.pop_back();
            return groups;
        }
    }
    return {};
}

GroupShell *find_hollow(Document &doc, const UUID &body)
{
    const auto groups = body_groups_of(doc, body);
    if (groups.empty() || groups.back()->get_type() != Group::Type::SHELL)
        return nullptr;
    return &doc.get_group<GroupShell>(groups.back()->m_uuid);
}

GroupShell *set_hollow(Document &doc, const UUID &body, double thickness, GroupShell::Open open)
{
    auto existing = find_hollow(doc, body);
    if (thickness <= 0) {
        if (existing) {
            ItemsToDelete items;
            items.groups.insert(existing->m_uuid);
            doc.set_group_generate_pending(doc.get_group_rel(existing->m_uuid, -1));
            items.append(doc.get_additional_items_to_delete(items));
            doc.delete_items(items);
        }
        return nullptr;
    }
    if (existing) {
        existing->m_thickness = thickness;
        existing->m_open = open;
        doc.set_group_update_solid_model_pending(existing->m_uuid);
        return existing;
    }
    const auto groups = body_groups_of(doc, body);
    if (groups.empty())
        return nullptr;
    const auto &last = *groups.back();
    if (!SolidModel::get_last_solid_model(doc, last, SolidModel::IncludeGroup::YES))
        return nullptr;
    auto &shell = doc.insert_group<GroupShell>(UUID::random(), last.m_uuid);
    shell.m_name = hollow_name;
    shell.m_thickness = thickness;
    shell.m_open = open;
    doc.set_group_generate_pending(shell.m_uuid);
    return &shell;
}

} // namespace dune3d

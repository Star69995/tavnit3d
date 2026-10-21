#include "object_folders.hpp"
#include "document.hpp"
#include "group/group.hpp"
#include <algorithm>

namespace dune3d {

std::vector<UUID> get_folder_members(const Document &doc, const std::string &folder)
{
    std::vector<UUID> r;
    if (folder.empty())
        return r;
    for (auto gr : doc.get_groups_sorted()) {
        if (gr->m_body && gr->m_body->m_folder == folder)
            r.push_back(gr->m_uuid);
    }
    return r;
}

std::vector<std::string> get_folder_names(const Document &doc)
{
    std::vector<std::string> r;
    for (auto gr : doc.get_groups_sorted()) {
        if (gr->m_body && !gr->m_body->m_folder.empty()
            && std::ranges::find(r, gr->m_body->m_folder) == r.end())
            r.push_back(gr->m_body->m_folder);
    }
    return r;
}

void set_body_folder(Document &doc, const UUID &body, const std::string &folder)
{
    doc.get_group(body).m_body->m_folder = folder;
}

void rename_folder(Document &doc, const std::string &from, const std::string &to)
{
    for (const auto &uu : get_folder_members(doc, from))
        doc.get_group(uu).m_body->m_folder = to;
}

std::string new_folder_name(const Document &doc)
{
    const auto names = get_folder_names(doc);
    std::string name = "Group";
    for (int n = 2; std::ranges::find(names, name) != names.end(); n++)
        name = "Group " + std::to_string(n);
    return name;
}

namespace {
std::vector<UUID> groups_of(const Document &doc, const UUID &body)
{
    std::vector<UUID> r;
    for (const auto &bg : doc.get_groups_by_body()) {
        if (bg.get_group().m_uuid == body) {
            for (auto gr : bg.groups)
                r.push_back(gr->m_uuid);
        }
    }
    return r;
}
} // namespace

bool apply_folder_drop(Document &doc, const FolderDrop &d, std::string *new_group)
{
    using Where = FolderDrop::Where;
    const bool drag_folder = d.folder.size();
    const auto moving = drag_folder ? get_folder_members(doc, d.folder) : std::vector<UUID>{d.body};
    if (moving.empty())
        return false;
    auto is_moving = [&moving](const UUID &u) { return std::ranges::find(moving, u) != moving.end(); };

    std::string dest;   // the group they end up in ("" = loose); a dragged group keeps its own name unless merged
    UUID anchor;        // the object to go next to
    bool after = true;
    std::string created;
    if (d.target_body) {
        if (is_moving(d.target_body) || !doc.get_groups().contains(d.target_body))
            return false;
        const auto target_folder = doc.get_group(d.target_body).m_body->m_folder;
        anchor = d.target_body;
        after = d.where != Where::BEFORE;
        if (d.where == Where::INTO && target_folder.empty() && !drag_folder) {
            created = new_folder_name(doc); // two loose objects: a group of them
            dest = created;
        }
        else {
            dest = target_folder;
        }
        if (drag_folder && dest.size()) {
            // groups don't nest: beside the target's group instead
            const auto members = get_folder_members(doc, target_folder);
            anchor = after ? members.back() : members.front();
            dest.clear();
        }
    }
    else {
        const auto members = get_folder_members(doc, d.target_folder);
        if (members.empty() || (drag_folder && d.target_folder == d.folder))
            return false;
        if (d.where == Where::INTO) {
            dest = d.target_folder;
            anchor = members.back();
            after = true;
        }
        else {
            after = d.where == Where::AFTER;
            anchor = after ? members.back() : members.front();
        }
    }
    // the anchor may itself be one of the moved objects (the last one of a group dropped on its own row)
    if (is_moving(anchor)) {
        UUID other;
        for (const auto &m : get_folder_members(doc, drag_folder ? d.folder : doc.get_group(anchor).m_body->m_folder)) {
            if (!is_moving(m))
                other = m;
        }
        anchor = other;
    }

    if (anchor) {
        // each moved object goes after the previous one, which starts after the anchor (or before it: after
        // whatever is before the anchor and isn't moving)
        UUID group_after;
        if (after) {
            const auto g = groups_of(doc, anchor);
            if (g.empty())
                return false;
            group_after = g.back();
        }
        else {
            const auto bodies = doc.get_groups_by_body();
            for (const auto &bg : bodies) {
                if (bg.get_group().m_uuid == anchor)
                    break;
                if (!is_moving(bg.get_group().m_uuid))
                    group_after = bg.groups.back()->m_uuid;
            }
            if (!group_after)
                return false;
        }
        for (const auto &body : moving) {
            auto g = groups_of(doc, body);
            if (g.empty())
                return false;
            if (g.back() != group_after && !doc.reorder_group(g, group_after))
                return false;
            group_after = g.back();
        }
    }
    if (created.size())
        set_body_folder(doc, d.target_body, created);
    for (const auto &body : moving) {
        if (!drag_folder || (!d.target_body && d.where == Where::INTO))
            set_body_folder(doc, body, dest);
    }
    if (new_group)
        *new_group = created;
    return true;
}

} // namespace dune3d

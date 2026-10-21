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

} // namespace dune3d

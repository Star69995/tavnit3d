#pragma once
#include "util/uuid.hpp"
#include <string>
#include <vector>

namespace dune3d {
class Document;

/*
 * Groups of objects ("folders"): objects that were put in the same group by name
 * (Body::m_folder). They stay separate objects - nothing is joined - but are
 * listed together and move together.
 */

// the objects (bodies, by their first group) in the group, in document order
std::vector<UUID> get_folder_members(const Document &doc, const std::string &folder);
// all group names in use, in the order they first appear
std::vector<std::string> get_folder_names(const Document &doc);
// puts the object in the group (empty name: takes it out); the caller rebuilds
void set_body_folder(Document &doc, const UUID &body, const std::string &folder);
// renames the group for all its objects; the caller rebuilds
void rename_folder(Document &doc, const std::string &from, const std::string &to);

} // namespace dune3d

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

// the next free name for a new group: "Group", "Group 2", ...
std::string new_folder_name(const Document &doc);

// A drag in the list of objects: an object or a whole group, dropped before / after / onto another
// object or a group's row.
struct FolderDrop {
    UUID body;           // what is dragged: one object ...
    std::string folder;  // ... or (if not empty) a whole group
    UUID target_body;    // where: on an object ...
    std::string target_folder; // ... or on a group's row (if no target_body)
    enum class Where { BEFORE, AFTER, INTO } where = Where::AFTER;
};
// Moves things as the drop says: next to the target in the document's order and into (or out of) its
// group. An object dropped INTO a loose object makes a new group of the two; groups don't nest. False
// (nothing more changed) if it can't be done, e.g. a join would then come before what it joins; the caller
// rebuilds. new_group, if given, gets the name of a group made by the drop.
bool apply_folder_drop(Document &doc, const FolderDrop &drop, std::string *new_group = nullptr);

} // namespace dune3d

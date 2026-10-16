#pragma once
#include "util/uuid.hpp"
#include "group/group_shell.hpp"

namespace dune3d {
class Document;

// The "hollow" step of a body: a shell group at the end of the body (before
// its rounded edges, if any).
GroupShell *find_hollow(Document &doc, const UUID &body);

// Hollows the body out with walls this thick, open on the given side, adding
// or updating its shell group; thickness 0 makes it solid again. Returns the
// group, or nullptr. The caller rebuilds.
GroupShell *set_hollow(Document &doc, const UUID &body, double thickness, GroupShell::Open open);

} // namespace dune3d

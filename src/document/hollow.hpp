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

// How round the edges can be: with walls, two roundings meet on a rim, so each a bit less
// than half the wall; otherwise no limit known beforehand.
double get_max_round_edges(Document &doc, const UUID &body);

// Like set_round_edges / set_hollow, but they rebuild, and when the new value doesn't work
// (too round for the object, walls too thick) they go back to `fallback` and return false.
// Making the walls thinner than the rounding allows makes the rounding smaller too.
bool try_round_edges(Document &doc, const UUID &body, double radius, double fallback);
bool try_hollow(Document &doc, const UUID &body, double thickness, GroupShell::Open open, double fallback);

} // namespace dune3d

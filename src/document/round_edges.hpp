#pragma once
#include "util/uuid.hpp"

namespace dune3d {
class Document;
class GroupFillet;

// The "rounded edges" step of a body: a fillet group at the end of the body
// that rounds all its edges, if there is one.
GroupFillet *find_round_edges(Document &doc, const UUID &body);

// Rounds all edges of a body (seams of curved faces excepted) with the given
// radius, adding or updating its rounded-edges fillet group; radius 0 removes
// it. Returns the fillet group, or nullptr. The caller rebuilds.
GroupFillet *set_round_edges(Document &doc, const UUID &body, double radius);

} // namespace dune3d

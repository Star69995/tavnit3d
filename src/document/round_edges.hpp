#pragma once
#include "util/uuid.hpp"
#include <set>

class TopoDS_Shape;

namespace dune3d {
class Document;
class GroupFillet;

// The "rounded edges" step of a body: a fillet group at the very end of the body
// that rounds all its edges, if there is one.
GroupFillet *find_round_edges(Document &doc, const UUID &body);

// Rounds all edges of a body (seams of curved faces excepted) with the given
// radius, adding or updating its rounded-edges fillet group; radius 0 removes
// it. Returns the fillet group, or nullptr. The caller rebuilds.
GroupFillet *set_round_edges(Document &doc, const UUID &body, double radius);

// Edge indices as numbered by SolidModelOcc (TopExp_Explorer order, repeats
// skipped) for every edge that can be rounded: not degenerate, not a seam
// where a curved face meets itself.
std::set<unsigned int> get_roundable_edges(const TopoDS_Shape &shape);

} // namespace dune3d

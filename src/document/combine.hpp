#pragma once
#include "util/uuid.hpp"
#include "document/group/igroup_solid_model.hpp"

namespace dune3d {
class Document;
class Group;

// Combines the final solids of two bodies (given by their first groups) into a
// new body after all groups: join, subtract (second from first) or intersect.
// The sources stay as they are, so they can still be edited; callers usually
// hide them. The caller is responsible for rebuilding.
Group &add_combination(Document &doc, const UUID &body_a, const UUID &body_b, IGroupSolidModel::Operation op);

// A combination takes a source body's solid from a given group, its last one when combined.
// When a step is added to or removed from the end of that body (rounding, hollowing), the
// combinations using group `from` are pointed at `to` instead, so they see the change
// (and deleting the step doesn't delete them).
void repoint_combinations(Document &doc, const UUID &from, const UUID &to);

} // namespace dune3d

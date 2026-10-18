#pragma once
#include "util/uuid.hpp"
#include <glm/glm.hpp>

namespace dune3d {
class Document;
class Group;

// Copies a body (given by its first group) - its groups, the entities and
// constraints drawn in them - and inserts the copy after all groups, as a new
// body. If the body has an X/Y position (like the ready-made shapes) the copy
// is moved by `offset`. Returns the copy's first group; the caller rebuilds.
Group &duplicate_body(Document &doc, const UUID &body, const glm::dvec2 &offset = {0, 0});

// Gives a body that stands on a shared workplane (X/Y dimensions from its origin) a workplane
// of its own, in the same place, and points its sketch, dimensions and steps at it. Its origin
// can then be moved in 3D, so the body can be lifted (see get_part_position). False if the
// body has no workplane to start from; true if it has one now. The caller rebuilds.
bool give_own_workplane(Document &doc, const UUID &body);

} // namespace dune3d

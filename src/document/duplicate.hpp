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

} // namespace dune3d

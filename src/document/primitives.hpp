#pragma once
#include "util/uuid.hpp"

namespace dune3d {
class Document;
class Group;

enum class Primitive { BOX, CYLINDER, SPHERE };

// Inserts a ready-made solid in a new body after the given group and returns
// the group holding its solid model. The caller is responsible for rebuilding.
Group &add_primitive(Document &doc, const UUID &after, Primitive primitive);

} // namespace dune3d

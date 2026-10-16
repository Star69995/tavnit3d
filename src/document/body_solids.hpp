#pragma once
#include "util/uuid.hpp"
#include <vector>

namespace dune3d {
class Document;
class SolidModelOcc;

// The final solid of each body as of group `upto` (inclusive), i.e. what's
// actually there; earlier states of a body are left out.
std::vector<const SolidModelOcc *> get_body_solids(const Document &doc, const UUID &upto);

} // namespace dune3d

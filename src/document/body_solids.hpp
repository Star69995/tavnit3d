#pragma once
#include "util/uuid.hpp"
#include <vector>

namespace dune3d {
class Document;
class SolidModelOcc;

struct BodySolid {
    UUID body; // the group that starts the body
    const SolidModelOcc *solid;
};

// The final solid of each body as of group `upto` (inclusive), i.e. what's
// actually there; earlier states of a body are left out.
std::vector<BodySolid> get_body_solids(const Document &doc, const UUID &upto);

} // namespace dune3d

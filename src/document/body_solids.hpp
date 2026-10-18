#pragma once
#include "util/uuid.hpp"
#include <vector>
#include <filesystem>
#include <functional>

namespace dune3d {
class Document;
class SolidModelOcc;

// decides whether a body (by its first group) takes part, e.g. only visible ones
using BodyFilter = std::function<bool(const UUID &body)>;

struct BodySolid {
    UUID body; // the group that starts the body
    const SolidModelOcc *solid;
};

// The final solid of each body as of group `upto` (inclusive), i.e. what's
// actually there; earlier states of a body are left out.
std::vector<BodySolid> get_body_solids(const Document &doc, const UUID &upto, const BodyFilter &filter = {});

// Writes the final solids of all bodies into one STL file, e.g. for 3D printing.
// Returns false if there's nothing to export.
bool export_bodies_stl(const Document &doc, const std::filesystem::path &path);

} // namespace dune3d

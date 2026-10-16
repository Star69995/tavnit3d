#pragma once
#include "util/uuid.hpp"
#include <filesystem>
#include <vector>

namespace dune3d {
class Document;

// Imports a STEP, IGES, glTF/GLB or OBJ file as objects after group `after`:
// one per colour and material in the file, with that colour and the closest
// material, standing on the XY plane next to what's there. The file is
// referenced (relative to containing_dir when that's where it is), not copied.
// Returns the new bodies (their first groups); empty if there was nothing to
// import. The caller is responsible for rebuilding.
std::vector<UUID> import_objects(Document &doc, const UUID &after, const std::filesystem::path &file,
                                 const std::filesystem::path &containing_dir);

} // namespace dune3d

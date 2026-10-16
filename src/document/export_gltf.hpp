#pragma once
#include "body_solids.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <filesystem>
#include <optional>

namespace dune3d {
class Document;

// a view to look at the scene from, in document coordinates (mm, z up);
// it looks along its local -z with y up, like the 3D view's camera
struct ExportCamera {
    glm::dvec3 position;
    glm::dquat orientation;
    bool orthographic = false;
    double yfov_deg = 45;      // perspective
    double half_height = 100;  // orthographic, mm
    double aspect = 16. / 9.;  // width / height
};

struct GltfExportOptions {
    BodyFilter filter; // which bodies; all if empty
    std::optional<ExportCamera> camera;
    bool lights = true;
};

// Writes the final solids as a binary glTF (.glb) with their colours and
// materials, stickers as textured layers on top, the document's lights
// (KHR_lights_punctual) and optionally a camera. Metres, y up, as glTF wants.
// Returns false if there's nothing to export.
bool export_gltf(const Document &doc, const std::filesystem::path &path, const GltfExportOptions &options = {});

} // namespace dune3d

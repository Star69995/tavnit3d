#pragma once
#include "util/uuid.hpp"
#include <glm/glm.hpp>
#include <optional>

namespace dune3d {
class Document;
class Group;

// A flat face of a solid hit by a ray, described by its plane.
struct FacePick {
    glm::dvec3 centroid; // centre of the face area
    glm::dvec3 normal;   // pointing out of the solid
    glm::dvec3 xdir;     // in-plane x axis
    glm::dvec3 bbox_min;
    glm::dvec3 bbox_max;
};

// Finds the nearest flat face along the ray among the final solids of the
// bodies up to and including group `upto`. Curved faces yield nothing.
std::optional<FacePick> pick_planar_face(const Document &doc, const UUID &upto, const glm::dvec3 &ray_origin,
                                         const glm::dvec3 &ray_dir);

// Inserts a sketch after `after` whose active workplane lies on the face. The
// workplane's rotation is locked and, where possible, its origin is pinned to
// an existing point on the face (nearest the face centre), so the sketch moves
// along when the body is resized.
Group &add_sketch_on_face(Document &doc, const UUID &after, const FacePick &face);

} // namespace dune3d

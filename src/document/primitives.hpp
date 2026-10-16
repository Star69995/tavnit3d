#pragma once
#include "util/uuid.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <vector>

namespace dune3d {
class Document;
class Group;
class GroupExtrude;
class EntityAndPoint;

enum class Primitive { BOX, CYLINDER, SPHERE, CONE, TUBE, TORUS, WEDGE };

// sizes in mm; which ones a shape uses depends on the shape
struct PrimitiveParams {
    double width = 20, depth = 20, height = 20; // box, wedge; height also cylinder, cone, tube
    double diameter = 20;                       // cylinder, sphere, cone (at the bottom), tube (outside)
    double inner_diameter = 12;                 // tube
    double ring_diameter = 20;                  // torus, through the middle of the ring
    double tube_diameter = 8;                   // torus, of the ring itself
};

// Where a shape stands when not on the active workplane: a workplane of its
// own, at origin and turned by rotation (z being the shape's up).
struct Placement {
    glm::dvec3 origin = {0, 0, 0};
    glm::dquat rotation = glm::dquat(1, 0, 0, 0);
};

// Inserts a ready-made solid in a new body after the given group, standing on
// the active workplane (XY without one) centred at pos, and returns the group
// holding its solid model. The caller is responsible for rebuilding.
Group &add_primitive(Document &doc, const UUID &after, Primitive primitive, const glm::dvec2 &pos = {0, 0},
                     const PrimitiveParams &params = {}, const std::optional<Placement> &placement = {});

// A body made by revolving a profile around an upright axis at pos: points
// (distance from the axis, height) from the bottom to the top. It's closed
// along the axis. smooth: one curve through the points instead of straight
// lines between them (the ends towards the axis stay straight).
Group &add_revolved_profile(Document &doc, const UUID &after, std::vector<glm::dvec2> profile, bool smooth,
                            const glm::dvec2 &pos = {0, 0}, const std::optional<Placement> &placement = {});

// A body made by extruding an outline (points around pos) upwards.
Group &add_extruded_outline(Document &doc, const UUID &after, const std::vector<glm::dvec2> &outline, double height,
                            bool smooth, const glm::dvec2 &pos = {0, 0},
                            const std::optional<Placement> &placement = {});

// Building blocks used by the shapes, also for shapes made elsewhere (text):
// an extrusion of a sketch with its height as a dimension (symmetric: both ways,
// height being half the thickness) ...
GroupExtrude &add_extrusion(Document &doc, const UUID &sketch, const UUID &wrkpl, double height,
                            bool symmetric = false);
// ... and a point's X/Y position as dimensions from the workplane origin.
void add_position(Document &doc, const UUID &sketch, const UUID &wrkpl, const EntityAndPoint &pt,
                  const glm::dvec2 &pos);

// A spot on the XY plane next to the solids there are as of group upto, for a
// new shape about footprint mm across, so that it doesn't overlap them.
glm::dvec2 find_free_spot(const Document &doc, const UUID &upto, double footprint = 20);

} // namespace dune3d

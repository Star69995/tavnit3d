#pragma once
#include "util/uuid.hpp"
#include <glm/glm.hpp>

namespace dune3d {
class Document;
class Group;
class GroupExtrude;
class EntityAndPoint;

enum class Primitive { BOX, CYLINDER, SPHERE, CONE, TUBE, TORUS, WEDGE };

// Inserts a ready-made solid in a new body after the given group, standing on
// the active workplane (XY without one) centred at pos, and returns the group
// holding its solid model. The caller is responsible for rebuilding.
Group &add_primitive(Document &doc, const UUID &after, Primitive primitive, const glm::dvec2 &pos = {0, 0});

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

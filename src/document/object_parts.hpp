#pragma once
#include "util/uuid.hpp"
#include "object_dimensions.hpp"
#include <glm/glm.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dune3d {
class Document;

/*
 * The parts of a joined object (Combine, or a recipe that joins shapes, like a
 * table from a top and four legs), so they can still be changed from the
 * joined object: their sizes, where they are, and the parts touching a part
 * that changes size keep their place against it (the top stays on the legs).
 */

// The objects at the leaves of the object's combinations (bodies, by their first group),
// in order; empty for an object that isn't made by combining.
std::vector<UUID> get_object_parts(const Document &doc, const UUID &body);

// Parts that are alike - the same kind of shape, the same sizes, rounding and hollowing,
// wherever they are - are changed together, like a table's legs.
struct PartSet {
    std::string name; // "Leg" for "Leg 1" ... "Leg 4"
    std::vector<UUID> parts;
};
std::vector<PartSet> group_alike_parts(const Document &doc, const std::vector<UUID> &parts);

// Where a part is: a workplane of its own (made with a Placement), moved in 3D, or
// X/Y dimensions from a workplane's origin (standing on it), which can't go up or down.
struct PartPosition {
    UUID body;
    UUID group; // to solve again
    UUID wrkpl; // its own workplane ...
    glm::dvec3 origin = {0, 0, 0};
    UUID cx, cy; // ... or these
    glm::dvec2 xy = {0, 0};
    glm::dvec3 ax = {1, 0, 0}, ay = {0, 1, 0}; // the X/Y directions
};
std::optional<PartPosition> get_part_position(const Document &doc, const UUID &body);
// puts the part where it was then, moved by delta; the caller rebuilds
void set_part_position(Document &doc, const PartPosition &pos, const glm::dvec3 &delta);

struct Bounds {
    glm::dvec3 lo, hi;
};
// of the body's final solid
std::optional<Bounds> get_body_bounds(const Document &doc, const UUID &body);

// How the sides of a part move when one of its dimensions (by its object panel label) goes
// from old_value to new_value; zero for what doesn't change its outside.
struct SideChange {
    glm::dvec3 lo = {0, 0, 0}, hi = {0, 0, 0};
};
SideChange get_side_change(const std::string &label, double old_value, double new_value, const Bounds &bounds);

// Taken before parts change size: where everything was. Then follow_size_change moves the parts
// touching the changed ones by how their sides moved: what stands on top goes up with it (and
// what stands on that), what's under it or beside it moves with the side it's near.
struct PartsFollow {
    std::vector<UUID> changed;
    std::map<UUID, Bounds> bounds;
    std::map<UUID, PartPosition> positions;
    std::vector<ObjectDimension> dims; // of the first changed part
    bool usable = false;               // the changed parts are upright, so their sides are along X/Y/Z
};
PartsFollow begin_parts_follow(const Document &doc, const std::vector<UUID> &parts,
                               const std::vector<UUID> &changed);
void follow_size_change(Document &doc, const PartsFollow &follow, const SideChange &change);

// Sets dimension `index` (as get_object_dimensions lists them) of each changed part, which are
// alike, to value, and moves what touches them along. Values are from when follow was taken,
// so this can be called again and again while dragging. The caller rebuilds.
void set_parts_dimension(Document &doc, const PartsFollow &follow, size_t index, double value);

} // namespace dune3d

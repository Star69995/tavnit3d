#pragma once
#include "util/uuid.hpp"
#include <string>
#include <vector>

namespace dune3d {
class Document;

// The dimensions of an object, as the object panel shows them: friendly
// labels ("Width", "Diameter", "X", ...) for the ready-made shapes.
struct ObjectDimension {
    std::string label;
    UUID constraint;
    UUID group;
    double scale; // shown value = datum * scale
    double value; // as shown
};
std::vector<ObjectDimension> get_object_dimensions(const Document &doc, const UUID &body);

} // namespace dune3d

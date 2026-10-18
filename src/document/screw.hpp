#pragma once
#include "nlohmann/json_fwd.hpp"
#include <string>

namespace dune3d {
using json = nlohmann::json;

enum class ScrewHead {
    HEX,          // hexagon bolt
    CAP,          // socket head with a hexagon socket
    PAN,          // rounded head with a slot
    COUNTERSUNK,  // flat cone head with a slot
};

struct ScrewParams {
    ScrewHead head = ScrewHead::HEX;
    double diameter = 8;  // nominal (M8), mm
    double length = 30;   // under the head, mm
    bool thread = true;   // ridges on the shank; false: a plain pin
};

// Standard sizes (ISO metric coarse); other diameters are scaled from the nearest.
double screw_pitch(double diameter);
// "M8x30"
std::string screw_label(const ScrewParams &params);

// The screw as a recipe (see recipe.hpp): standing on its tip, head up. The shank is a
// cylinder at the thread's root with a real helical ridge wound round it (GroupThread),
// at most 32 turns from the tip; the rest of a longer shank is plain.
json make_screw_recipe(const ScrewParams &params);

} // namespace dune3d

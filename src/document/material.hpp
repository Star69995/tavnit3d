#pragma once
#include "group/group.hpp"
#include <vector>

namespace dune3d {

// How a body's material looks in physically based terms, as used for export
// (glTF metallic/roughness) and when guessing a material for imported files.
// The 3D view approximates the same looks in face-fragment.glsl.
struct MaterialPbr {
    float metallic = 0;
    float roughness = .5;
    float alpha = 1; // < 1: see-through
};

MaterialPbr get_material_pbr(Body::Material mat);

// the closest of our materials to some other program's material
Body::Material material_from_pbr(const MaterialPbr &pbr);

const std::vector<Body::Material> &get_all_materials(); // in the order shown in menus
const char *get_material_label(Body::Material mat);

} // namespace dune3d

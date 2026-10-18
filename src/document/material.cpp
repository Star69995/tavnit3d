#include "material.hpp"

namespace dune3d {

MaterialPbr get_material_pbr(Body::Material mat)
{
    using M = Body::Material;
    switch (mat) {
    case M::SATIN:
        return {.metallic = 0, .roughness = .45};
    case M::MATTE:
        return {.metallic = 0, .roughness = .9};
    case M::GLOSSY:
        return {.metallic = 0, .roughness = .12};
    case M::METAL:
        return {.metallic = 1, .roughness = .3};
    case M::GLASS:
        return {.metallic = 0, .roughness = .05, .alpha = .3};
    case M::WOOD:
        return {.metallic = 0, .roughness = .6};
    }
    return {};
}

Body::Material material_from_pbr(const MaterialPbr &pbr)
{
    using M = Body::Material;
    if (pbr.alpha < .9)
        return M::GLASS;
    if (pbr.metallic > .5)
        return M::METAL;
    if (pbr.roughness < .3)
        return M::GLOSSY;
    if (pbr.roughness > .75)
        return M::MATTE;
    return M::SATIN;
}

const std::vector<Body::Material> &get_all_materials()
{
    using M = Body::Material;
    static const std::vector<M> all = {M::SATIN, M::MATTE, M::GLOSSY, M::METAL, M::GLASS, M::WOOD};
    return all;
}

const char *get_material_label(Body::Material mat)
{
    using M = Body::Material;
    switch (mat) {
    case M::SATIN:
        return "Satin";
    case M::MATTE:
        return "Matte";
    case M::GLOSSY:
        return "Glossy";
    case M::METAL:
        return "Metal";
    case M::GLASS:
        return "Glass";
    case M::WOOD:
        return "Wood";
    }
    return "";
}

} // namespace dune3d

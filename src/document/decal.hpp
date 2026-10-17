#pragma once
#include "util/uuid.hpp"
#include "canvas/face.hpp"
#include "nlohmann/json_fwd.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace dune3d {
using json = nlohmann::json;
class PictureData;

// A picture stuck onto a body's surface ("sticker"). It's projected straight
// along its normal onto the faces that look that way, so it also wraps around
// gently curved surfaces such as a cylinder's side. The picture itself is kept
// like a picture entity's: in the document's .pic folder, by m_data_uuid.
class Decal {
public:
    Decal();
    explicit Decal(const json &j);
    json serialize() const;

    UUID m_uuid;
    UUID m_data_uuid;
    std::shared_ptr<const PictureData> m_data;

    glm::dvec3 m_origin = {0, 0, 0}; // centre, on the surface
    glm::dvec3 m_normal = {0, 0, 1}; // pointing out of the surface
    // where the centre of the body's bounds was when the sticker was placed;
    // the sticker moves along when the body does
    glm::dvec3 m_anchor = {0, 0, 0};
    double m_width = 20; // the height follows from the picture's aspect ratio (a tile's size when repeating)
    double m_angle = 0;  // degrees, counter-clockwise about the normal
    // a pattern: the picture repeats over every surface facing along the normal, not just once
    bool m_repeat = false;

    double get_height() const;
};

// triangles, three consecutive vertices each
struct DecalMesh {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs; // (0, 0) is the picture's top left corner
};

glm::dvec3 get_bounds_center(const face::Faces &faces);

// The part of `faces` covered by the sticker, lifted off the surface by
// `offset` so it shows on top of it.
DecalMesh make_decal_mesh(const Decal &decal, const face::Faces &faces, float offset = 0);

} // namespace dune3d

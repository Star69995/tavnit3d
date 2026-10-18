#pragma once
#include "util/uuid.hpp"
#include "util/color.hpp"
#include "nlohmann/json_fwd.hpp"
#include <glm/glm.hpp>
#include <string>

namespace dune3d {
using json = nlohmann::json;

// A lamp in the scene, for nicer pictures: it lights the 3D view (no shadows)
// and goes along into glTF exports and renders. Belongs to the document, not
// to any object.
class Light {
public:
    enum class Type { POINT, SPOT, SUN };

    Light();
    explicit Light(const json &j);
    json serialize() const;

    UUID m_uuid;
    std::string m_name = "Light";
    Type m_type = Type::POINT;
    Color m_color = {1, 1, 1};
    // how strongly it lights what it's aimed at: 1 is about as bright as
    // the default view lighting, regardless of how far away the light is
    double m_brightness = 1;
    glm::dvec3 m_position = {0, 0, 100}; // mm
    glm::dvec3 m_target = {0, 0, 0};     // what it's aimed at; spot and sun shine this way
    double m_spot_angle = 45;            // full cone angle, degrees

    glm::dvec3 get_direction() const; // from the light towards the target
    double get_target_distance() const;
    // the dot just below the lamp (or the aimed-at point) that is dragged to change its height
    glm::dvec3 get_height_handle(bool target) const;

    // for exports, in physical units of a scene measured in metres:
    // W for point and spot lights (like Blender), W/m² for the sun
    double get_power_watts() const;
};

const char *get_light_type_label(Light::Type type);

} // namespace dune3d

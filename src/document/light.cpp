#include "light.hpp"
#include "nlohmann/json.hpp"
#include "util/glm_util.hpp"
#include "util/json_util.hpp"
#include "util/lut.hpp"
#include <numbers>

namespace dune3d {

static const LutEnumStr<Light::Type> light_type_lut = {
        {"point", Light::Type::POINT},
        {"spot", Light::Type::SPOT},
        {"sun", Light::Type::SUN},
};

// irradiance (W/m²) for brightness 1, about what a sun of strength 3 gives in Blender
static constexpr double base_irradiance = 3;

Light::Light() : m_uuid(UUID::random())
{
}

Light::Light(const json &j)
    : m_uuid(j.at("uuid").get<UUID>()), m_name(j.value("name", "Light")),
      m_type(light_type_lut.lookup_opt(j.value("type", "point")).value_or(Type::POINT)),
      m_color(j.at("color").get<Color>()), m_brightness(j.value("brightness", 1.)),
      m_position(j.at("position").get<glm::dvec3>()), m_target(j.at("target").get<glm::dvec3>()),
      m_spot_angle(j.value("spot_angle", 45.))
{
}

json Light::serialize() const
{
    return json{
            {"uuid", m_uuid},
            {"name", m_name},
            {"type", light_type_lut.lookup_reverse(m_type)},
            {"color", m_color},
            {"brightness", m_brightness},
            {"position", m_position},
            {"target", m_target},
            {"spot_angle", m_spot_angle},
    };
}

glm::dvec3 Light::get_direction() const
{
    const auto d = m_target - m_position;
    if (glm::length(d) < 1e-9)
        return {0, 0, -1};
    return glm::normalize(d);
}

double Light::get_target_distance() const
{
    return std::max(glm::length(m_target - m_position), 1e-3);
}

double Light::get_power_watts() const
{
    if (m_type == Type::SUN)
        return m_brightness * base_irradiance;
    // a point light of power P gives P / (4 pi d²) at distance d
    const double d = get_target_distance() / 1000;
    return m_brightness * base_irradiance * 4 * std::numbers::pi * d * d;
}

const char *get_light_type_label(Light::Type type)
{
    switch (type) {
    case Light::Type::POINT:
        return "Lamp";
    case Light::Type::SPOT:
        return "Spotlight";
    case Light::Type::SUN:
        return "Sun";
    }
    return "";
}

} // namespace dune3d

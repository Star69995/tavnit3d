#include "lighting_uniforms.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace dune3d {

static constexpr size_t max_lights = 8; // as in lighting.glsl

void LightingUniforms::realize(GLuint program)
{
    m_n_lights_loc = glGetUniformLocation(program, "n_lights");
    m_pos_loc = glGetUniformLocation(program, "light_pos");
    m_dir_loc = glGetUniformLocation(program, "light_dir");
    m_color_loc = glGetUniformLocation(program, "light_color");
    m_dist_loc = glGetUniformLocation(program, "light_dist");
}

void LightingUniforms::load(const std::vector<CanvasLight> &lights) const
{
    const auto n = std::min(lights.size(), max_lights);
    std::array<glm::vec4, max_lights> pos, dir, color;
    std::array<float, max_lights> dist;
    for (size_t i = 0; i < n; i++) {
        const auto &l = lights.at(i);
        const auto half = glm::radians(std::clamp(l.spot_angle, 1.f, 179.f) / 2);
        pos.at(i) = glm::vec4(l.position, static_cast<float>(l.type));
        dir.at(i) = glm::vec4(l.direction, std::cos(half));
        color.at(i) = glm::vec4(l.color, std::cos(half * .8f));
        dist.at(i) = l.distance;
    }
    glUniform1i(m_n_lights_loc, n);
    if (!n)
        return;
    glUniform4fv(m_pos_loc, n, glm::value_ptr(pos.front()));
    glUniform4fv(m_dir_loc, n, glm::value_ptr(dir.front()));
    glUniform4fv(m_color_loc, n, glm::value_ptr(color.front()));
    glUniform1fv(m_dist_loc, n, dist.data());
}

} // namespace dune3d

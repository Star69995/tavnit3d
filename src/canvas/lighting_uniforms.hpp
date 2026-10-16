#pragma once
#include <epoxy/gl.h>
#include <glm/glm.hpp>
#include <vector>

namespace dune3d {

// a scene light as the shaders see it (see shaders/lighting.glsl)
struct CanvasLight {
    enum class Type { POINT, SPOT, SUN };
    Type type = Type::POINT;
    glm::vec3 position;
    glm::vec3 direction;
    glm::vec3 color; // times brightness
    float distance = 100; // to what it's aimed at
    float spot_angle = 45; // full cone, degrees
};

// uploads the scene lights to a program that includes lighting.glsl
class LightingUniforms {
public:
    void realize(GLuint program);
    void load(const std::vector<CanvasLight> &lights) const;

private:
    GLint m_n_lights_loc = -1;
    GLint m_pos_loc = -1;
    GLint m_dir_loc = -1;
    GLint m_color_loc = -1;
    GLint m_dist_loc = -1;
};

} // namespace dune3d

#include "light_shape.hpp"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace dune3d {

namespace {
constexpr int n_around = 24;

class MeshBuilder {
public:
    explicit MeshBuilder(const face::Color &color)
    {
        m_face.color = color;
    }

    size_t vertex(const glm::dvec3 &p, const glm::dvec3 &n)
    {
        m_face.vertices.emplace_back(p.x, p.y, p.z);
        m_face.normals.emplace_back(n.x, n.y, n.z);
        m_pos.push_back(p);
        return m_pos.size() - 1;
    }

    // wound so that it faces the way the vertex normals do
    void triangle(size_t a, size_t b, size_t c)
    {
        const auto cr = glm::cross(m_pos.at(b) - m_pos.at(a), m_pos.at(c) - m_pos.at(a));
        const auto &n = m_face.normals.at(a);
        if (cr.x * n.x + cr.y * n.y + cr.z * n.z < 0)
            std::swap(b, c);
        m_face.triangle_indices.emplace_back(a, b, c);
    }

    face::Face take()
    {
        return std::move(m_face);
    }

private:
    face::Face m_face;
    std::vector<glm::dvec3> m_pos;
};

struct Frame {
    glm::dvec3 axis, u, v;
    explicit Frame(const glm::dvec3 &d)
    {
        axis = glm::normalize(d);
        const glm::dvec3 helper = std::abs(axis.z) < .9 ? glm::dvec3(0, 0, 1) : glm::dvec3(1, 0, 0);
        u = glm::normalize(glm::cross(axis, helper));
        v = glm::cross(axis, u);
    }
    glm::dvec3 ring(double angle) const
    {
        return u * std::cos(angle) + v * std::sin(angle);
    }
};

face::Face sphere(const glm::dvec3 &c, double r, const face::Color &color)
{
    MeshBuilder mb(color);
    const int rows = n_around / 2;
    std::vector<size_t> idx;
    for (int i = 0; i <= rows; i++) {
        const double th = glm::pi<double>() * i / rows;
        for (int j = 0; j <= n_around; j++) {
            const double ph = glm::two_pi<double>() * j / n_around;
            const glm::dvec3 n{std::sin(th) * std::cos(ph), std::sin(th) * std::sin(ph), std::cos(th)};
            idx.push_back(mb.vertex(c + n * r, n));
        }
    }
    const auto at = [&](int i, int j) { return idx.at(i * (n_around + 1) + j); };
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < n_around; j++) {
            mb.triangle(at(i, j), at(i + 1, j), at(i, j + 1));
            mb.triangle(at(i, j + 1), at(i + 1, j), at(i + 1, j + 1));
        }
    }
    return mb.take();
}

// a cone or cylinder from a (radius ra) to b (radius rb), with flat ends
face::Face tube(const glm::dvec3 &a, const glm::dvec3 &b, double ra, double rb, const face::Color &color,
                bool cap_a = true, bool cap_b = true)
{
    MeshBuilder mb(color);
    const auto len = glm::length(b - a);
    const Frame fr(b - a);
    for (int j = 0; j < n_around; j++) {
        const double a0 = glm::two_pi<double>() * j / n_around, a1 = glm::two_pi<double>() * (j + 1) / n_around;
        const auto n0 = glm::normalize(fr.ring(a0) * len + fr.axis * (ra - rb));
        const auto n1 = glm::normalize(fr.ring(a1) * len + fr.axis * (ra - rb));
        const auto p0 = mb.vertex(a + fr.ring(a0) * ra, n0), p1 = mb.vertex(a + fr.ring(a1) * ra, n1);
        const auto q0 = mb.vertex(b + fr.ring(a0) * rb, n0), q1 = mb.vertex(b + fr.ring(a1) * rb, n1);
        mb.triangle(p0, p1, q0);
        mb.triangle(p1, q1, q0);
        for (const bool end : {false, true}) {
            if (end ? !cap_b : !cap_a)
                continue;
            const auto c = end ? b : a;
            const auto r = end ? rb : ra;
            const auto n = end ? fr.axis : -fr.axis;
            mb.triangle(mb.vertex(c, n), mb.vertex(c + fr.ring(a0) * r, n), mb.vertex(c + fr.ring(a1) * r, n));
        }
    }
    return mb.take();
}

face::Color bright(const Color &c)
{
    // a lit lamp: its colour pushed towards white so it reads as glowing
    return {std::min(1., .2 + c.r * .8), std::min(1., .2 + c.g * .8), std::min(1., .2 + c.b * .8)};
}

face::Color beam_color(const Color &c)
{
    // white light would vanish on the light background: a warm tint then, the colour itself when it has one
    const double sat = std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b});
    const double t = std::clamp((sat - .05) / .25, 0., 1.);
    return {1 + (c.r - 1) * t, .8 + (c.g - .8) * t, .3 + (c.b - .3) * t};
}
} // namespace

double get_light_shape_size(const Light &light)
{
    return std::clamp(light.get_target_distance() * .07, 3., 30.);
}

face::Faces make_light_beam(const Light &light, double r)
{
    face::Faces faces;
    const auto col = beam_color(light.m_color);
    const auto &pos = light.m_position;
    const auto dir = light.get_direction();
    const double len = light.get_target_distance();
    switch (light.m_type) {
    case Light::Type::POINT:
        // goes out all around
        faces.push_back(sphere(pos, r * 4, col));
        break;
    case Light::Type::SPOT: {
        const double rad = std::min(len * std::tan(glm::radians(light.m_spot_angle / 2)), len * 4);
        faces.push_back(tube(pos, pos + dir * len, 0, rad, col, false, false));
        break;
    }
    case Light::Type::SUN:
        // far away, so all rays are parallel: one wide column towards the target
        faces.push_back(tube(pos, pos + dir * len, r * 2.6, r * 2.6, col, false, false));
        break;
    }
    return faces;
}

face::Faces make_light_shape(const Light &light, double r)
{
    face::Faces faces;
    const face::Color metal{.28, .29, .32}, cap{.62, .63, .66};
    const auto glow = bright(light.m_color);
    const auto &pos = light.m_position;
    const auto dir = light.get_direction();

    switch (light.m_type) {
    case Light::Type::POINT:
        // a bulb on a screw cap that points up
        faces.push_back(sphere(pos, r, glow));
        faces.push_back(tube(pos + glm::dvec3(0, 0, r * .8), pos + glm::dvec3(0, 0, r * 1.7), r * .55, r * .55, cap));
        faces.push_back(tube(pos + glm::dvec3(0, 0, r * 1.7), pos + glm::dvec3(0, 0, r * 2.0), r * .55, r * .3, metal));
        break;
    case Light::Type::SPOT:
        // a lamp housing behind the lens, which sits at the position and faces the target
        faces.push_back(tube(pos - dir * r * .1, pos - dir * r * 2.2, r, r * .6, metal));
        faces.push_back(tube(pos, pos - dir * r * .12, r * .85, r * .85, glow));
        faces.push_back(tube(pos - dir * r * 2.2, pos - dir * r * 2.9, r * .6, r * .35, cap));
        break;
    case Light::Type::SUN: {
        // a disc with rays all around it, in the plane across the light's direction
        faces.push_back(sphere(pos, r, glow));
        const Frame fr(dir);
        const int rays = 10;
        for (int i = 0; i < rays; i++) {
            const auto d = fr.ring(glm::two_pi<double>() * i / rays);
            faces.push_back(tube(pos + d * r * 1.25, pos + d * r * 2.3, r * .22, 0, glow));
        }
        break;
    }
    }
    return faces;
}

} // namespace dune3d

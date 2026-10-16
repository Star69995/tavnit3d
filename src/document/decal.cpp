#include "decal.hpp"
#include "nlohmann/json.hpp"
#include "util/glm_util.hpp"
#include "util/json_util.hpp"
#include "util/picture_data.hpp"
#include <glm/gtx/rotate_vector.hpp>
#include <algorithm>
#include <limits>

namespace dune3d {

Decal::Decal() : m_uuid(UUID::random())
{
}

Decal::Decal(const json &j)
    : m_uuid(j.at("uuid").get<UUID>()), m_data_uuid(j.at("data").get<UUID>()),
      m_origin(j.at("origin").get<glm::dvec3>()), m_normal(j.at("normal").get<glm::dvec3>()),
      m_anchor(j.at("anchor").get<glm::dvec3>()), m_width(j.at("width").get<double>()),
      m_angle(j.value("angle", 0.))
{
}

json Decal::serialize() const
{
    return json{
            {"uuid", m_uuid},     {"data", m_data_uuid}, {"origin", m_origin}, {"normal", m_normal},
            {"anchor", m_anchor}, {"width", m_width},    {"angle", m_angle},
    };
}

double Decal::get_height() const
{
    if (!m_data || !m_data->m_width)
        return m_width;
    return m_width * m_data->m_height / m_data->m_width;
}

glm::dvec3 get_bounds_center(const face::Faces &faces)
{
    glm::dvec3 lo{std::numeric_limits<double>::infinity()};
    glm::dvec3 hi{-std::numeric_limits<double>::infinity()};
    for (const auto &face : faces) {
        for (const auto &v : face.vertices) {
            const glm::dvec3 p{v.x, v.y, v.z};
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
    }
    if (lo.x > hi.x)
        return {0, 0, 0};
    return (lo + hi) / 2.;
}

namespace {
struct ClipVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

ClipVertex lerp(const ClipVertex &a, const ClipVertex &b, float t)
{
    return {
            .pos = glm::mix(a.pos, b.pos, t),
            .normal = glm::mix(a.normal, b.normal, t),
            .uv = glm::mix(a.uv, b.uv, t),
    };
}

// keeps the part of the polygon where uv[axis] * sign >= limit * sign
std::vector<ClipVertex> clip(const std::vector<ClipVertex> &poly, int axis, float limit, float sign)
{
    std::vector<ClipVertex> out;
    for (size_t i = 0; i < poly.size(); i++) {
        const auto &a = poly.at(i);
        const auto &b = poly.at((i + 1) % poly.size());
        const float da = (a.uv[axis] - limit) * sign;
        const float db = (b.uv[axis] - limit) * sign;
        if (da >= 0)
            out.push_back(a);
        if ((da >= 0) != (db >= 0))
            out.push_back(lerp(a, b, da / (da - db)));
    }
    return out;
}
} // namespace

DecalMesh make_decal_mesh(const Decal &decal, const face::Faces &faces, float offset)
{
    DecalMesh mesh;
    if (!decal.m_data)
        return mesh;
    const double w = decal.m_width;
    const double h = decal.get_height();
    if (w <= 0 || h <= 0)
        return mesh;

    const auto n = glm::normalize(decal.m_normal);
    glm::dvec3 up = std::abs(n.z) < .95 ? glm::dvec3(0, 0, 1) : glm::dvec3(0, 1, 0);
    up = glm::normalize(up - n * glm::dot(up, n));
    up = glm::rotate(up, glm::radians(decal.m_angle), n);
    const auto right = glm::cross(up, n);
    const auto origin = decal.m_origin + (get_bounds_center(faces) - decal.m_anchor);
    // how far in front of or behind the surface point it still sticks
    const double depth = std::max(w, h) * .6;

    for (const auto &face : faces) {
        for (const auto &[ia, ib, ic] : face.triangle_indices) {
            std::vector<ClipVertex> poly;
            double d_min = std::numeric_limits<double>::infinity();
            double d_max = -d_min;
            glm::vec2 uv_min{std::numeric_limits<float>::infinity()};
            glm::vec2 uv_max{-std::numeric_limits<float>::infinity()};
            for (const auto i : {ia, ib, ic}) {
                const auto &v = face.vertices.at(i);
                const auto &vn = face.normals.at(i);
                const glm::dvec3 p{v.x, v.y, v.z};
                const auto rel = p - origin;
                const glm::vec2 uv(glm::dot(rel, right) / w + .5, .5 - glm::dot(rel, up) / h);
                const double d = glm::dot(rel, n);
                d_min = std::min(d_min, d);
                d_max = std::max(d_max, d);
                uv_min = glm::min(uv_min, uv);
                uv_max = glm::max(uv_max, uv);
                poly.push_back({.pos = {v.x, v.y, v.z}, .normal = {vn.x, vn.y, vn.z}, .uv = uv});
            }
            if (d_max < -depth || d_min > depth)
                continue;
            if (uv_max.x < 0 || uv_min.x > 1 || uv_max.y < 0 || uv_min.y > 1)
                continue;
            // only the side that looks towards the sticker's normal
            const auto tri_n = glm::cross(poly.at(1).pos - poly.at(0).pos, poly.at(2).pos - poly.at(0).pos);
            const auto len = glm::length(tri_n);
            if (len == 0 || glm::dot(glm::dvec3(tri_n / len), n) < .1)
                continue;

            poly = clip(poly, 0, 0, 1);
            poly = clip(poly, 0, 1, -1);
            poly = clip(poly, 1, 0, 1);
            poly = clip(poly, 1, 1, -1);
            for (size_t i = 2; i < poly.size(); i++) {
                for (const auto &v : {poly.at(0), poly.at(i - 1), poly.at(i)}) {
                    const auto vn = glm::normalize(v.normal);
                    mesh.positions.push_back(v.pos + vn * offset);
                    mesh.normals.push_back(vn);
                    mesh.uvs.push_back(v.uv);
                }
            }
        }
    }
    return mesh;
}

} // namespace dune3d

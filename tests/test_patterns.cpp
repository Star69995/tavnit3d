// Patterns: the generated tiles, and how a repeating sticker covers a surface.
#include "document/decal.hpp"
#include "document/pattern.hpp"
#include "preferences/preferences.hpp"
#include "util/picture_data.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <iostream>

using namespace dune3d;

const Preferences &Preferences::get()
{
    static Preferences the_preferences;
    return the_preferences;
}

static int failures = 0;

static void check(bool ok, const std::string &what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok)
        failures++;
}

// a flat square, side mm wide, in the XY plane looking up, as two triangles
static face::Faces square(float side)
{
    face::Face f;
    const float h = side / 2;
    f.vertices = {{-h, -h, 0}, {h, -h, 0}, {h, h, 0}, {-h, h, 0}};
    f.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    f.triangle_indices = {{0, 1, 2}, {0, 2, 3}};
    return {f};
}

static float uv_extent(const DecalMesh &m, int axis)
{
    float lo = 1e30f, hi = -1e30f;
    for (const auto &uv : m.uvs) {
        lo = std::min(lo, uv[axis]);
        hi = std::max(hi, uv[axis]);
    }
    return hi - lo;
}

static float area(const DecalMesh &m)
{
    float a = 0;
    for (size_t i = 0; i + 2 < m.positions.size(); i += 3)
        a += glm::length(glm::cross(m.positions[i + 1] - m.positions[i], m.positions[i + 2] - m.positions[i])) / 2;
    return a;
}

int main()
{
    std::cout << "pattern pictures" << std::endl;
    check(get_all_patterns().size() == 4, "four patterns");
    for (const auto &p : get_all_patterns()) {
        const auto pic = make_pattern_picture(p.pattern, get_all_pattern_colours().at(0), 64);
        check(pic && pic->m_width == 64 && pic->m_height == 64 && pic->m_data.size() == 64 * 64,
              std::string(p.label) + ": a 64 x 64 tile");
        const auto marked = std::count_if(pic->m_data.begin(), pic->m_data.end(), [](uint32_t c) { return c >> 24; });
        const auto total = (long)pic->m_data.size();
        check(marked > total / 10 && marked < total * 9 / 10, std::string(p.label) + ": part marked, part clear");
    }
    {
        const auto a = make_pattern_picture(Pattern::CHECKER, get_all_pattern_colours().at(2), 64);
        const auto b = make_pattern_picture(Pattern::CHECKER, get_all_pattern_colours().at(2), 64);
        check(a->m_uuid != b->m_uuid, "each picture gets its own UUID");
        // red: bytes R G B A in memory
        const uint32_t red = 214 | (48u << 8) | (49u << 16) | (0xffu << 24);
        check(a->m_data[0 * 64 + 40] == red && a->m_data[0] == 0, "checker: squares alternate, colour as chosen");
    }

    std::cout << "repeating stickers" << std::endl;
    const auto tile = make_pattern_picture(Pattern::DOTS, get_all_pattern_colours().at(0), 32);
    const auto faces = square(20);
    Decal once; // an ordinary sticker, 5 mm wide, in the middle
    once.m_data = tile;
    once.m_origin = {0, 0, 0};
    once.m_normal = {0, 0, 1};
    once.m_anchor = {0, 0, 0};
    once.m_width = 5;
    const auto one = make_decal_mesh(once, faces);
    check(!one.positions.empty() && uv_extent(one, 0) <= 1.0001f, "a sticker stays within its picture");
    check(std::abs(area(one) - 25) < .01, "and covers only its 5 x 5 mm");

    Decal rep = once;
    rep.m_repeat = true;
    const auto many = make_decal_mesh(rep, faces);
    check(std::abs(area(many) - 400) < .01, "a pattern covers the whole 20 x 20 mm face");
    check(std::abs(uv_extent(many, 0) - 4) < .001 && std::abs(uv_extent(many, 1) - 4) < .001,
          "5 mm tiles on 20 mm: the picture repeats 4 times each way");

    Decal sideways = rep;
    sideways.m_normal = {1, 0, 0}; // looking along x: the face is edge-on
    check(make_decal_mesh(sideways, faces).positions.empty(), "faces turned away get no pattern");
    Decal steep = rep;
    steep.m_normal = glm::normalize(glm::dvec3(1, 0, 1.2)); // about 40 degrees off the face's normal
    check(!make_decal_mesh(steep, faces).positions.empty(), "slightly tilted faces still get it");
    steep.m_normal = glm::normalize(glm::dvec3(1, 0, .5)); // over 60 degrees
    check(make_decal_mesh(steep, faces).positions.empty(), "steep ones don't (no smearing)");

    std::cout << "saving" << std::endl;
    {
        const auto j = rep.serialize();
        check(j.value("repeat", false), "a pattern is saved as repeating");
        check(Decal(j).m_repeat, "and loads back as one");
        check(!once.serialize().contains("repeat") && !Decal(once.serialize()).m_repeat,
              "ordinary stickers don't carry the flag");
    }

    std::cout << (failures ? "FAILED" : "all passed") << std::endl;
    return failures ? 1 : 0;
}

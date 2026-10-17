#include "pattern.hpp"
#include "util/picture_data.hpp"
#include <algorithm>
#include <cmath>

namespace dune3d {

const std::vector<PatternInfo> &get_all_patterns()
{
    static const std::vector<PatternInfo> r = {
            {Pattern::STRIPES, "Stripes"},
            {Pattern::DOTS, "Dots"},
            {Pattern::CHECKER, "Checker"},
            {Pattern::GRID, "Grid"},
    };
    return r;
}

const std::vector<PatternColour> &get_all_pattern_colours()
{
    static const std::vector<PatternColour> r = {
            {"Dark", 40, 40, 44},    {"White", 245, 245, 245}, {"Red", 214, 48, 49},
            {"Blue", 40, 100, 220},  {"Green", 40, 160, 90},   {"Yellow", 245, 195, 30},
    };
    return r;
}

namespace {
// is the pixel (x, y) of a size x size tile part of the mark? u, v in 0..1
bool is_mark(Pattern pattern, double u, double v)
{
    switch (pattern) {
    case Pattern::STRIPES:
        // diagonal stripes that meet at the tile's edges
        return std::fmod(u + v, 1.0) < .5;
    case Pattern::DOTS: {
        const double dx = u - .5, dy = v - .5;
        return dx * dx + dy * dy < .3 * .3;
    }
    case Pattern::CHECKER:
        return (u < .5) != (v < .5);
    case Pattern::GRID:
        return u < .08 || u > .92 || v < .08 || v > .92;
    }
    return false;
}
} // namespace

std::shared_ptr<const PictureData> make_pattern_picture(Pattern pattern, const PatternColour &colour,
                                                        unsigned int size)
{
    size = std::max(size, 8u);
    std::vector<uint32_t> px(size * size, 0); // transparent
    // bytes R, G, B, A in memory, as the texture upload expects
    const uint32_t on = colour.r | (uint32_t(colour.g) << 8) | (uint32_t(colour.b) << 16) | (0xffu << 24);
    for (unsigned int y = 0; y < size; y++) {
        for (unsigned int x = 0; x < size; x++) {
            if (is_mark(pattern, (x + .5) / size, (y + .5) / size))
                px[y * size + x] = on;
        }
    }
    return std::make_shared<PictureData>(UUID::random(), size, size, std::move(px));
}

} // namespace dune3d

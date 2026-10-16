#pragma once
#include "util/color.hpp"
#include <map>

namespace dune3d {

enum class ColorP;

class Appearance {
public:
    Appearance();
    std::map<ColorP, Color> colors;
    const Color &get_color(const ColorP &color) const;

    unsigned int msaa = 4;

    // workplane grid: colour and opacity of regular and major lines; major
    // lines every grid_major_every cells (0: none)
    Color grid_color = {.24, .49, .95};
    float grid_alpha = .30;
    float grid_major_alpha = .60;
    unsigned int grid_major_every = 5;
    float line_width = 2.5;
    bool selection_glow = false;
};
} // namespace dune3d

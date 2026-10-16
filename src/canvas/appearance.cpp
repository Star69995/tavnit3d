#include "appearance.hpp"
#include "color_palette.hpp"

namespace dune3d {

Appearance::Appearance()
{
    colors[ColorP::BACKGROUND_BOTTOM] = {0.80, 0.83, 0.88};
    colors[ColorP::BACKGROUND_TOP] = {0.96, 0.97, 0.98};
    colors[ColorP::CONSTRAINT] = {0.55, 0.36, 0.96};
    colors[ColorP::CONSTRUCTION_ENTITY] = {0.20, 0.55, 0.60};
    colors[ColorP::CONSTRUCTION_POINT] = {0.20, 0.55, 0.60};
    colors[ColorP::ENTITY] = {0.11, 0.14, 0.19};
    colors[ColorP::HOVER] = {0.18, 0.42, 0.94};
    colors[ColorP::INACTIVE_ENTITY] = {0.55, 0.58, 0.64};
    colors[ColorP::INACTIVE_POINT] = {0.62, 0.70, 0.86};
    colors[ColorP::POINT] = {0.18, 0.42, 0.94};
    colors[ColorP::SELECTED] = {1.00, 0.48, 0.10};
    colors[ColorP::SELECTED_HOVER] = {1.00, 0.62, 0.30};
    colors[ColorP::SOLID_MODEL] = {0.84, 0.87, 0.92};
    colors[ColorP::OTHER_BODY_SOLID_MODEL] = {0.72, 0.76, 0.82};
    colors[ColorP::HIGHLIGHT] = {.18, .42, .94};
    colors[ColorP::SELECTION_BOX] = {0.18, 0.42, 0.94};
    colors[ColorP::ERROR_OVERLAY] = {1, 0.2, 0.2};
}

const Color &Appearance::get_color(const ColorP &color) const
{
    static const Color default_color{1, 0, 1};
    if (colors.contains(color))
        return colors.at(color);
    else
        return default_color;
}

} // namespace dune3d

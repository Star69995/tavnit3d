#pragma once
#include <memory>
#include <string>
#include <vector>

namespace dune3d {
class PictureData;

// Ready-made repeating patterns for stickers: a tile picture whose background is
// transparent, so the object's own colour shows between the marks.
enum class Pattern { STRIPES, DOTS, CHECKER, GRID };

struct PatternInfo {
    Pattern pattern;
    const char *label;
};
const std::vector<PatternInfo> &get_all_patterns();

struct PatternColour {
    const char *label;
    unsigned char r, g, b;
};
const std::vector<PatternColour> &get_all_pattern_colours();

// A square tile (size x size pixels) that repeats seamlessly. The picture
// gets a fresh UUID, so it is saved with the document like any other.
std::shared_ptr<const PictureData> make_pattern_picture(Pattern pattern, const PatternColour &colour,
                                                        unsigned int size = 128);

} // namespace dune3d

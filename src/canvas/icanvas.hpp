#pragma once
#include <glm/glm.hpp>
#include <tuple>
#include "face.hpp"
#include <glm/gtx/quaternion.hpp>
#include <memory>

namespace dune3d {

class SelectableRef;
class PictureData;
struct DecalMesh;

namespace IconTexture {
enum class IconTextureID;
}

class ICanvas {
public:
    enum class VertexType { LINE, GLYPH, GLYPH_3D, ICON, FACE_GROUP, PICTURE, SELECTION_INVISIBLE };
    struct VertexRef {
        VertexType type;
        size_t index;
        unsigned int chunk;

        friend auto operator<=>(const VertexRef &, const VertexRef &) = default;
        friend bool operator==(const VertexRef &, const VertexRef &) = default;
    };

    enum class LineStyle {
        DEFAULT = 0,
        THIN = (1 << 0),
        GRID = (1 << 1),       // thin, in the grid colour and opacity
        GRID_MAJOR = (1 << 2), // same, for major grid lines
    };

    virtual void set_chunk(unsigned int chunk) = 0;

    virtual void clear() = 0;
    virtual VertexRef draw_point(glm::vec3 p) = 0;
    virtual VertexRef draw_line(glm::vec3 from, glm::vec3 to) = 0;
    virtual VertexRef draw_screen_line(glm::vec3 origin, glm::vec3 direction) = 0;
    virtual std::vector<VertexRef> draw_bitmap_text(glm::vec3 p, float size, const std::string &rtext) = 0;
    virtual std::vector<VertexRef> draw_bitmap_text_3d(glm::vec3 p, const glm::quat &norm, float size,
                                                       const std::string &rtext) = 0;

    // virtual void add_faces(const face::Faces &faces) = 0;
    enum class FaceColor { AS_IS, SOLID_MODEL, OTHER_BODY_SOLID_MODEL };
    // material: 0 satin, 1 matte, 2 glossy, 3 metal (see face-fragment.glsl), optionally
    // or'ed with face_material_highlight
    static constexpr unsigned int face_material_highlight = 0x100;
    virtual VertexRef add_face_group(const face::Faces &faces, glm::vec3 origin, glm::quat normal,
                                     FaceColor face_color, unsigned int material = 0) = 0;
    virtual VertexRef draw_icon(IconTexture::IconTextureID id, glm::vec3 origin, glm::vec2 shift,
                                glm::vec3 v = {NAN, NAN, NAN}) = 0;
    virtual VertexRef draw_point(glm::vec3 origin, IconTexture::IconTextureID id) = 0;
    virtual VertexRef draw_picture(const std::array<glm::vec3, 4> &corners,
                                   std::shared_ptr<const PictureData> data) = 0;
    // a sticker on a solid's surface, see document/decal.hpp
    virtual void add_decal(const struct DecalMesh &mesh, std::shared_ptr<const PictureData> data, bool repeat = false) = 0;

    virtual void add_selectable(const VertexRef &vref, const SelectableRef &sref) = 0;
    virtual void set_selection_invisible(bool selection_invisible) = 0;

    virtual void save() = 0;
    virtual void restore() = 0;

    // tracked by save/restore
    virtual void set_vertex_inactive(bool inactive) = 0;
    virtual void set_vertex_constraint(bool c) = 0;
    virtual void set_vertex_construction(bool c) = 0;
    virtual void set_no_points(bool c) = 0;
    virtual void set_line_style(LineStyle style) = 0;
    virtual void set_transform(const glm::mat4 &transform) = 0;

    virtual void set_override_selectable(const SelectableRef &sr) = 0;
    virtual void unset_override_selectable() = 0;

    virtual void update_bbox() = 0;
};
} // namespace dune3d

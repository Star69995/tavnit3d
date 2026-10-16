#pragma once
#include "base_renderer.hpp"
#include "texture_cache.hpp"
#include <map>
#include "util/uuid.hpp"
#include <memory>

namespace dune3d {

class PictureData;

class PictureRenderer : public BaseRenderer {
public:
    PictureRenderer(class Canvas &c);
    void realize();
    void render();
    void push();

private:
    GLuint m_vao;
    GLuint m_vbo;

    GLuint m_corners_loc;
    GLuint m_tex_loc;
    GLuint m_flags_loc;
    GLuint m_pick_base_loc;

    TextureCache m_textures;

    class PictureWithDepth;
};
} // namespace dune3d

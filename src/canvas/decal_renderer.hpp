#pragma once
#include <epoxy/gl.h>
#include "texture_cache.hpp"

namespace dune3d {

// Stickers: textured triangles lying on the solids' surfaces, drawn after the
// opaque faces. They don't take part in picking, so clicking one picks the
// body underneath.
class DecalRenderer {
public:
    DecalRenderer(class Canvas &c);
    void realize();
    void render();
    void push();

private:
    Canvas &m_ca;
    GLuint m_program;
    GLuint m_vao;
    GLuint m_vbo;

    GLuint m_view_loc;
    GLuint m_proj_loc;
    GLuint m_tex_loc;
    GLuint m_cam_normal_loc;
    GLuint m_studio_lighting_loc;
    GLuint m_clipping_value_loc;
    GLuint m_clipping_op_loc;

    TextureCache m_textures;
};
} // namespace dune3d

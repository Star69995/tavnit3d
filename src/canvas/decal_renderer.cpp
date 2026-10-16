#include "decal_renderer.hpp"
#include "canvas.hpp"
#include "gl_util.hpp"
#include "util/picture_data.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace dune3d {

DecalRenderer::DecalRenderer(Canvas &ca) : m_ca(ca), m_textures(GL_LINEAR)
{
}

void DecalRenderer::realize()
{
    m_program = gl_create_program_from_resource("/org/dune3d/dune3d/canvas/shaders/decal-vertex.glsl",
                                                "/org/dune3d/dune3d/canvas/shaders/decal-fragment.glsl", nullptr);
    GLuint position_index = glGetAttribLocation(m_program, "position");
    GLuint normal_index = glGetAttribLocation(m_program, "normal");
    GLuint uv_index = glGetAttribLocation(m_program, "uv");

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    using V = CanvasChunk::DecalVertex;
    glEnableVertexAttribArray(position_index);
    glVertexAttribPointer(position_index, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void *)offsetof(V, x));
    glEnableVertexAttribArray(normal_index);
    glVertexAttribPointer(normal_index, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void *)offsetof(V, nx));
    glEnableVertexAttribArray(uv_index);
    glVertexAttribPointer(uv_index, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void *)offsetof(V, u));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    GET_LOC(this, view);
    GET_LOC(this, proj);
    GET_LOC(this, tex);
    GET_LOC(this, cam_normal);
    GET_LOC(this, studio_lighting);
    GET_LOC(this, clipping_value);
    GET_LOC(this, clipping_op);
    m_lighting.realize(m_program);
}

void DecalRenderer::push()
{
    size_t n = 0;
    std::set<UUID> needed;
    for (const auto &chunk : m_ca.m_chunks) {
        n += chunk.m_decal_vertices.size();
        for (const auto &decal : chunk.m_decals) {
            m_textures.get(decal.data);
            needed.insert(decal.data->m_uuid);
        }
    }
    m_textures.evict_except(needed);

    using V = CanvasChunk::DecalVertex;
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(V) * n, nullptr, GL_STATIC_DRAW);
    size_t offset = 0;
    for (const auto chunk_id : m_ca.get_chunk_ids()) {
        auto &chunk = m_ca.m_chunks.at(chunk_id);
        chunk.m_decal_offset = offset;
        glBufferSubData(GL_ARRAY_BUFFER, offset * sizeof(V), sizeof(V) * chunk.m_decal_vertices.size(),
                        chunk.m_decal_vertices.data());
        offset += chunk.m_decal_vertices.size();
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static int get_clipping_op(const ClippingPlanes::Plane &plane)
{
    if (!plane.enabled)
        return 0;
    return plane.operation == ClippingPlanes::Plane::Operation::CLIP_GREATER ? 1 : -1;
}

void DecalRenderer::render()
{
    glUseProgram(m_program);
    glBindVertexArray(m_vao);
    glUniformMatrix4fv(m_view_loc, 1, GL_FALSE, glm::value_ptr(m_ca.m_viewmat));
    glUniformMatrix4fv(m_proj_loc, 1, GL_FALSE, glm::value_ptr(m_ca.m_projmat));
    glUniform3fv(m_cam_normal_loc, 1, glm::value_ptr(m_ca.m_cam_normal));
    glUniform1i(m_studio_lighting_loc, m_ca.m_studio_lighting ? 1 : 0);
    m_lighting.load(m_ca.m_lights);
    const auto &cp = m_ca.m_clipping_planes;
    glUniform3f(m_clipping_value_loc, cp.x.value, cp.y.value, cp.z.value);
    glUniform3i(m_clipping_op_loc, get_clipping_op(cp.x), get_clipping_op(cp.y), get_clipping_op(cp.z));
    glActiveTexture(GL_TEXTURE2);
    glUniform1i(m_tex_loc, 2);

    // blended over the surface, leaving picking and the selection glow alone
    glEnablei(GL_BLEND, 0);
    glDepthMask(GL_FALSE);
    glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glColorMaski(2, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    for (const auto chunk_id : m_ca.get_chunk_ids()) {
        const auto &chunk = m_ca.m_chunks.at(chunk_id);
        for (const auto &decal : chunk.m_decals) {
            glBindTexture(GL_TEXTURE_2D, m_textures.get(decal.data->m_uuid));
            glDrawArrays(GL_TRIANGLES, chunk.m_decal_offset + decal.offset, decal.count);
        }
    }
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDisablei(GL_BLEND, 0);

    glBindVertexArray(0);
    glUseProgram(0);
}

} // namespace dune3d

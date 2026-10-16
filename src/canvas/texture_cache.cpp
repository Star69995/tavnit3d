#include "texture_cache.hpp"
#include "util/picture_data.hpp"
#include <vector>

namespace dune3d {

GLuint TextureCache::get(const std::shared_ptr<const PictureData> &d)
{
    if (auto it = m_textures.find(d->m_uuid); it != m_textures.end())
        return it->second.second;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, d->m_width, d->m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, d->m_data.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    m_mag_filter == GL_NEAREST ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, m_mag_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_textures.emplace(std::piecewise_construct, std::forward_as_tuple(d->m_uuid), std::forward_as_tuple(d, tex));
    return tex;
}

void TextureCache::evict_except(const std::set<UUID> &needed)
{
    std::vector<UUID> evict;
    for (const auto &[uu, it] : m_textures) {
        if (!needed.count(uu))
            evict.push_back(uu);
    }
    for (const auto &uu : evict) {
        glDeleteTextures(1, &m_textures.at(uu).second);
        m_textures.erase(uu);
    }
}

} // namespace dune3d

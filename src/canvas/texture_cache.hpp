#pragma once
#include <epoxy/gl.h>
#include <map>
#include <memory>
#include <set>
#include "util/uuid.hpp"

namespace dune3d {

class PictureData;

// GL textures for pictures, by the picture data's UUID
class TextureCache {
public:
    explicit TextureCache(GLint mag_filter) : m_mag_filter(mag_filter)
    {
    }
    GLuint get(const std::shared_ptr<const PictureData> &data); // uploads it if needed
    GLuint get(const UUID &uu) const
    {
        return m_textures.at(uu).second;
    }
    void evict_except(const std::set<UUID> &needed);

private:
    const GLint m_mag_filter;
    std::map<UUID, std::pair<std::shared_ptr<const PictureData>, GLuint>> m_textures;
};

} // namespace dune3d

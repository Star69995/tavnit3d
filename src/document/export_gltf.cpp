#include "export_gltf.hpp"
#include "document.hpp"
#include "group/group.hpp"
#include "material.hpp"
#include "decal.hpp"
#include "light.hpp"
#include "solid_model/solid_model_occ.hpp"
#include "util/picture_data.hpp"
#include "util/picture_load.hpp"
#include "util/branding.hpp"
#include "nlohmann/json.hpp"
#include <glm/gtc/quaternion.hpp>
#include <fstream>
#include <map>
#include <numbers>

namespace dune3d {

namespace {

constexpr double mm_to_m = 1e-3;
constexpr double watts_to_lumens = 683;

// document: mm, z up; glTF: m, y up
glm::vec3 to_gltf_point(const glm::dvec3 &p)
{
    return glm::vec3(p.x, p.z, -p.y) * static_cast<float>(mm_to_m);
}

glm::vec3 to_gltf_dir(const glm::dvec3 &d)
{
    return {d.x, d.z, -d.y};
}

const glm::dquat z_up_to_y_up = glm::angleAxis(-std::numbers::pi / 2, glm::dvec3(1, 0, 0));

float srgb_to_linear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

json linear_rgb(const Color &c)
{
    return json::array({srgb_to_linear(c.r), srgb_to_linear(c.g), srgb_to_linear(c.b)});
}

json quat_json(const glm::dquat &q)
{
    return json::array({q.x, q.y, q.z, q.w});
}

class GltfWriter {
public:
    json m_j;
    std::vector<uint8_t> m_bin;
    std::set<std::string> m_extensions;

    GltfWriter()
    {
        m_j["asset"] = {{"version", "2.0"}, {"generator", std::string(app_name)}};
        for (const auto key : {"buffers", "bufferViews", "accessors", "meshes", "materials", "nodes", "images",
                               "textures", "samplers", "cameras"})
            m_j[key] = json::array();
    }

    size_t add_view(const void *data, size_t size, std::optional<int> target)
    {
        while (m_bin.size() % 4)
            m_bin.push_back(0);
        const auto offset = m_bin.size();
        m_bin.insert(m_bin.end(), static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
        json v = {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", size}};
        if (target)
            v["target"] = *target;
        m_j["bufferViews"].push_back(v);
        return m_j["bufferViews"].size() - 1;
    }

    size_t add_accessor(const std::vector<glm::vec3> &v, bool with_bounds)
    {
        const auto view = add_view(v.data(), v.size() * sizeof(glm::vec3), 34962);
        json a = {{"bufferView", view}, {"componentType", 5126}, {"count", v.size()}, {"type", "VEC3"}};
        if (with_bounds && v.size()) {
            glm::vec3 lo = v.front(), hi = v.front();
            for (const auto &p : v) {
                lo = glm::min(lo, p);
                hi = glm::max(hi, p);
            }
            a["min"] = {lo.x, lo.y, lo.z};
            a["max"] = {hi.x, hi.y, hi.z};
        }
        m_j["accessors"].push_back(a);
        return m_j["accessors"].size() - 1;
    }

    size_t add_accessor(const std::vector<glm::vec2> &v)
    {
        const auto view = add_view(v.data(), v.size() * sizeof(glm::vec2), 34962);
        m_j["accessors"].push_back(
                {{"bufferView", view}, {"componentType", 5126}, {"count", v.size()}, {"type", "VEC2"}});
        return m_j["accessors"].size() - 1;
    }

    size_t add_accessor(const std::vector<uint32_t> &v)
    {
        const auto view = add_view(v.data(), v.size() * sizeof(uint32_t), 34963);
        m_j["accessors"].push_back(
                {{"bufferView", view}, {"componentType", 5125}, {"count", v.size()}, {"type", "SCALAR"}});
        return m_j["accessors"].size() - 1;
    }

    size_t add(const char *key, json item)
    {
        m_j[key].push_back(std::move(item));
        return m_j[key].size() - 1;
    }

    std::map<std::pair<UUID, bool>, size_t> m_textures; // by picture data and whether it tiles
    size_t get_texture(const PictureData &data, bool repeat = false)
    {
        if (auto it = m_textures.find({data.m_uuid, repeat}); it != m_textures.end())
            return it->second;
        if (m_j["samplers"].empty()) {
            // 0: clamped (stickers), 1: repeating (patterns)
            add("samplers", {{"magFilter", 9729}, {"minFilter", 9987}, {"wrapS", 33071}, {"wrapT", 33071}});
            add("samplers", {{"magFilter", 9729}, {"minFilter", 9987}, {"wrapS", 10497}, {"wrapT", 10497}});
        }
        const auto png = picture_data_to_png(data);
        const auto view = add_view(png.data(), png.size(), {});
        const auto image = add("images", {{"bufferView", view}, {"mimeType", "image/png"}});
        const auto tex = add("textures", {{"sampler", repeat ? 1 : 0}, {"source", image}});
        m_textures.emplace(std::make_pair(data.m_uuid, repeat), tex);
        return tex;
    }

    void write(const std::filesystem::path &path)
    {
        while (m_bin.size() % 4)
            m_bin.push_back(0);
        m_j["buffers"].push_back({{"byteLength", m_bin.size()}});
        if (m_extensions.size())
            m_j["extensionsUsed"] = m_extensions;
        // glTF doesn't like empty arrays
        for (auto it = m_j.begin(); it != m_j.end();) {
            if (it->is_array() && it->empty())
                it = m_j.erase(it);
            else
                ++it;
        }
        auto js = m_j.dump();
        while (js.size() % 4)
            js.push_back(' ');

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs)
            throw std::runtime_error("can't write the file");
        auto u32 = [&ofs](uint32_t v) { ofs.write(reinterpret_cast<const char *>(&v), 4); };
        u32(0x46546C67); // "glTF"
        u32(2);
        u32(12 + 8 + js.size() + 8 + m_bin.size());
        u32(js.size());
        u32(0x4E4F534A); // "JSON"
        ofs.write(js.data(), js.size());
        u32(m_bin.size());
        u32(0x004E4942); // "BIN"
        ofs.write(reinterpret_cast<const char *>(m_bin.data()), m_bin.size());
    }
};

json pbr_material(const std::string &name, const Color &color, Body::Material mat, GltfWriter &w)
{
    const auto pbr = get_material_pbr(mat);
    auto base = linear_rgb(color);
    base.push_back(pbr.alpha);
    json m = {
            {"name", name},
            {"pbrMetallicRoughness",
             {{"baseColorFactor", base}, {"metallicFactor", pbr.metallic}, {"roughnessFactor", pbr.roughness}}},
            {"doubleSided", true},
    };
    if (pbr.alpha < 1) {
        m["alphaMode"] = "BLEND";
        // renderers that know about it make it look like real glass
        m["extensions"]["KHR_materials_transmission"] = {{"transmissionFactor", 1}};
        w.m_extensions.insert("KHR_materials_transmission");
    }
    return m;
}

} // namespace

bool export_gltf(const Document &doc, const std::filesystem::path &path, const GltfExportOptions &options)
{
    GltfWriter w;
    json scene_nodes = json::array();

    // holes are only there to cut with
    const auto solids = get_body_solids(doc, UUID(), [&](const UUID &b) {
        return !doc.get_group(b).m_body->m_hole && (!options.filter || options.filter(b));
    });
    for (const auto &bs : solids) {
        const auto &body = *doc.get_group(bs.body).m_body;
        const auto &faces = bs.solid->m_faces;
        // faces may have a colour or material of their own (imported or joined ones): a primitive per look
        struct Prim {
            std::vector<glm::vec3> positions, normals;
            std::vector<uint32_t> indices;
        };
        std::map<std::tuple<float, float, float, unsigned char>, Prim> prims;
        for (const auto &face : faces) {
            auto &prim = prims[{face.color.r, face.color.g, face.color.b, face.material}];
            const auto base = prim.positions.size();
            for (size_t i = 0; i < face.vertices.size(); i++) {
                const auto &v = face.vertices.at(i);
                const auto &n = face.normals.at(i);
                prim.positions.push_back(to_gltf_point({v.x, v.y, v.z}));
                prim.normals.push_back(glm::normalize(to_gltf_dir({n.x, n.y, n.z})));
            }
            for (const auto &[a, b, c] : face.triangle_indices) {
                prim.indices.push_back(base + a);
                prim.indices.push_back(base + b);
                prim.indices.push_back(base + c);
            }
        }
        json primitives = json::array();
        for (const auto &[col, prim] : prims) {
            if (prim.indices.empty())
                continue;
            const auto &[r, g, b, face_material] = col;
            const auto material = w.add("materials", pbr_material(body.m_name, Color(r, g, b),
                                                                 face_material == face::Face::no_material
                                                                         ? body.m_material
                                                                         : static_cast<Body::Material>(face_material),
                                                                 w));
            primitives.push_back({
                    {"attributes",
                     {{"POSITION", w.add_accessor(prim.positions, true)},
                      {"NORMAL", w.add_accessor(prim.normals, false)}}},
                    {"indices", w.add_accessor(prim.indices)},
                    {"material", material},
            });
        }
        if (primitives.empty())
            continue;

        // stickers: their own primitives, lifted slightly off the surface
        for (const auto &decal : body.m_decals) {
            if (!decal.m_data)
                continue;
            const float offset = std::max(0.01, decal.m_width * 0.002);
            const auto mesh = make_decal_mesh(decal, faces, offset);
            if (mesh.positions.empty())
                continue;
            std::vector<glm::vec3> dp, dn;
            for (size_t i = 0; i < mesh.positions.size(); i++) {
                dp.push_back(to_gltf_point(mesh.positions.at(i)));
                dn.push_back(glm::normalize(to_gltf_dir(mesh.normals.at(i))));
            }
            const auto tex = w.get_texture(*decal.m_data, decal.m_repeat);
            const auto pbr = get_material_pbr(body.m_material);
            const auto mat = w.add("materials", {
                                                        {"name", body.m_name + " sticker"},
                                                        {"pbrMetallicRoughness",
                                                         {{"baseColorTexture", {{"index", tex}}},
                                                          {"metallicFactor", 0},
                                                          {"roughnessFactor", pbr.roughness}}},
                                                        {"alphaMode", "BLEND"},
                                                });
            primitives.push_back({
                    {"attributes",
                     {{"POSITION", w.add_accessor(dp, true)},
                      {"NORMAL", w.add_accessor(dn, false)},
                      {"TEXCOORD_0", w.add_accessor(mesh.uvs)}}},
                    {"material", mat},
            });
        }

        const auto mesh = w.add("meshes", {{"name", body.m_name}, {"primitives", primitives}});
        scene_nodes.push_back(w.add("nodes", {{"name", body.m_name}, {"mesh", mesh}}));
    }
    if (scene_nodes.empty())
        return false;

    if (options.lights && doc.m_lights.size()) {
        json lights = json::array();
        for (const auto &light : doc.m_lights) {
            const auto power = light.get_power_watts();
            json l = {{"name", light.m_name}, {"color", linear_rgb(light.m_color)}};
            switch (light.m_type) {
            case Light::Type::POINT:
                l["type"] = "point";
                l["intensity"] = power * watts_to_lumens / (4 * std::numbers::pi); // candela
                break;
            case Light::Type::SPOT: {
                l["type"] = "spot";
                l["intensity"] = power * watts_to_lumens / (4 * std::numbers::pi);
                const double outer = glm::radians(std::clamp(light.m_spot_angle, 1., 179.) / 2);
                l["spot"] = {{"innerConeAngle", outer * .8}, {"outerConeAngle", outer}};
            } break;
            case Light::Type::SUN:
                l["type"] = "directional";
                l["intensity"] = power * watts_to_lumens; // lux
                break;
            }
            lights.push_back(l);
            const auto dir = to_gltf_dir(light.get_direction());
            const glm::vec3 up = std::abs(dir.y) > .99 ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            const auto rot = glm::quatLookAt(glm::normalize(dir), up); // lights shine along -z
            const auto pos = to_gltf_point(light.m_position);
            scene_nodes.push_back(w.add("nodes", {
                                                         {"name", light.m_name},
                                                         {"translation", {pos.x, pos.y, pos.z}},
                                                         {"rotation", quat_json(glm::dquat(rot))},
                                                         {"extensions",
                                                          {{"KHR_lights_punctual", {{"light", lights.size() - 1}}}}},
                                                 }));
        }
        w.m_j["extensions"]["KHR_lights_punctual"] = {{"lights", lights}};
        w.m_extensions.insert("KHR_lights_punctual");
    }

    if (const auto &cam = options.camera) {
        json c;
        if (cam->orthographic) {
            const double ymag = cam->half_height * mm_to_m;
            c = {{"type", "orthographic"},
                 {"orthographic", {{"xmag", ymag * cam->aspect}, {"ymag", ymag}, {"znear", 0.0001}, {"zfar", 1000}}}};
        }
        else {
            c = {{"type", "perspective"},
                 {"perspective",
                  {{"yfov", glm::radians(cam->yfov_deg)}, {"aspectRatio", cam->aspect}, {"znear", 0.001}}}};
        }
        c["name"] = "View";
        const auto camera = w.add("cameras", c);
        const auto pos = to_gltf_point(cam->position);
        scene_nodes.push_back(w.add("nodes", {
                                                     {"name", "View"},
                                                     {"camera", camera},
                                                     {"translation", {pos.x, pos.y, pos.z}},
                                                     {"rotation", quat_json(z_up_to_y_up * cam->orientation)},
                                             }));
    }

    w.m_j["scenes"] = json::array({{{"nodes", scene_nodes}}});
    w.m_j["scene"] = 0;
    w.write(path);
    return true;
}

} // namespace dune3d

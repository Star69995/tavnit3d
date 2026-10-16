#include "workspace_view.hpp"
#include "nlohmann/json.hpp"
#include "util/glm_util.hpp"

namespace dune3d {

NLOHMANN_JSON_SERIALIZE_ENUM(CanvasProjection, {
                                                       {CanvasProjection::ORTHO, "ortho"},
                                                       {CanvasProjection::PERSP, "persp"},
                                               })

json WorkspaceView::serialize(const UUID &uu_doc) const
{
    json j;
    j["name"] = m_name;
    j["document"] = m_documents.at(uu_doc).serialize();
    j["center"] = glm::dvec3(m_center);
    j["cam_distance"] = m_cam_distance;
    j["projection"] = m_projection;
    j["cam_quat"] = m_cam_quat;
    j["curvature_comb_scale"] = m_curvature_comb_scale;
    j["show_construction_entities_from_previous_groups"] = m_show_construction_entities_from_previous_groups;
    j["hide_irrelevant_workplanes"] = m_hide_irrelevant_workplanes;
    j["grid_step"] = m_grid_step;
    j["studio_lighting"] = m_studio_lighting;
    j["snap_to_grid"] = m_snap_to_grid;
    j["show_other_sketches"] = m_show_other_sketches;
    return j;
}

bool WorkspaceView::document_is_visible(const UUID &uu_doc) const
{
    if (!m_documents.contains(uu_doc))
        return false;
    return m_documents.at(uu_doc).document_is_visible();
}

json WorkspaceView::serialize(const std::map<UUID, WorkspaceView> &views, const UUID &doc_uu)
{
    json wsvs;
    for (auto &[uu, wsv] : views) {
        if (wsv.document_is_visible(doc_uu)) {
            wsvs[uu] = wsv.serialize(doc_uu);
        }
    }
    return wsvs;
}

std::map<UUID, WorkspaceView> WorkspaceView::load_from_json(const json &j)
{
    std::map<UUID, WorkspaceView> r;
    for (const auto &[uu, it] : j.items()) {
        r.emplace(uu, it);
    }
    return r;
}

WorkspaceView::WorkspaceView() = default;

WorkspaceView::WorkspaceView(const json &j)
    : m_name(j.at("name").get<std::string>()), m_center(j.at("center").get<glm::dvec3>()),
      m_cam_distance(j.at("cam_distance").get<float>()), m_projection(j.at("projection").get<CanvasProjection>()),
      m_cam_quat(j.at("cam_quat").get<glm::dquat>()), m_curvature_comb_scale(j.value("curvature_comb_scale", 0.)),
      m_show_construction_entities_from_previous_groups(
              j.value("show_construction_entities_from_previous_groups", false)),
      m_hide_irrelevant_workplanes(j.value("hide_irrelevant_workplanes", false)),
      m_grid_step(j.value("grid_step", 0.)), m_studio_lighting(j.value("studio_lighting", true)),
      m_snap_to_grid(j.value("snap_to_grid", true)), m_show_other_sketches(j.value("show_other_sketches", false))
{
    m_documents.emplace(UUID{}, j.at("document"));
    if (m_name == "Default")
        m_name = "";
}

} // namespace dune3d

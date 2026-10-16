#include "round_edges.hpp"
#include "document/document.hpp"
#include "document/group/group_fillet.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <algorithm>
#include <list>
#include <ranges>

namespace dune3d {

static constexpr const char *round_edges_name = "Rounded edges";

namespace {
std::vector<const Group *> body_groups_of(const Document &doc, const UUID &body)
{
    for (const auto &bg : doc.get_groups_by_body()) {
        if (bg.get_group().m_uuid == body)
            return bg.groups;
    }
    return {};
}

} // namespace

std::set<unsigned int> get_roundable_edges(const TopoDS_Shape &shape)
{
    TopTools_IndexedDataMapOfShapeListOfShape edge_faces;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edge_faces);
    std::set<unsigned int> r;
    std::list<TopoDS_Shape> seen;
    unsigned int idx = 0;
    for (TopExp_Explorer ex(shape, TopAbs_EDGE); ex.More(); ex.Next(), idx++) {
        if (std::ranges::any_of(seen, [&ex](const auto &o) { return o.IsSame(ex.Current()); }))
            continue;
        seen.push_back(ex.Current());
        const auto edge = TopoDS::Edge(ex.Current());
        if (BRep_Tool::Degenerated(edge))
            continue;
        if (!edge_faces.Contains(edge))
            continue;
        const auto &faces = edge_faces.FindFromKey(edge);
        if (faces.Extent() < 2 || faces.First().IsSame(faces.Last()))
            continue; // a seam
        r.insert(idx);
    }
    return r;
}

GroupFillet *find_round_edges(Document &doc, const UUID &body)
{
    const auto groups = body_groups_of(doc, body);
    if (groups.empty())
        return nullptr;
    auto last = groups.back();
    if (last->get_type() != Group::Type::FILLET || last->m_name != round_edges_name)
        return nullptr;
    return &doc.get_group<GroupFillet>(last->m_uuid);
}

GroupFillet *set_round_edges(Document &doc, const UUID &body, double radius)
{
    auto existing = find_round_edges(doc, body);
    if (radius <= 0) {
        if (existing) {
            ItemsToDelete items;
            items.groups.insert(existing->m_uuid);
            doc.set_group_generate_pending(doc.get_group_rel(existing->m_uuid, -1));
            items.append(doc.get_additional_items_to_delete(items));
            doc.delete_items(items);
        }
        return nullptr;
    }
    if (existing) {
        existing->m_radius = radius;
        doc.set_group_update_solid_model_pending(existing->m_uuid);
        return existing;
    }
    const auto groups = body_groups_of(doc, body);
    if (groups.empty())
        return nullptr;
    const auto &last = *groups.back();
    auto sm = dynamic_cast<const SolidModelOcc *>(SolidModel::get_last_solid_model(doc, last, SolidModel::IncludeGroup::YES));
    if (!sm || sm->m_shape_acc.IsNull())
        return nullptr;
    auto edges = get_roundable_edges(sm->m_shape_acc);
    if (edges.empty())
        return nullptr; // e.g. a sphere
    auto &fillet = doc.insert_group<GroupFillet>(UUID::random(), last.m_uuid);
    fillet.m_name = round_edges_name;
    fillet.m_radius = radius;
    fillet.m_edges = std::move(edges);
    fillet.m_all_edges = true; // found again whenever the shape changes, e.g. when hollowed
    doc.set_group_generate_pending(fillet.m_uuid);
    return &fillet;
}

} // namespace dune3d

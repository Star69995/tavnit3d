#include "hollow.hpp"
#include "document/document.hpp"
#include "document/solid_model/solid_model.hpp"
#include "document/round_edges.hpp"
#include "document/group/group_fillet.hpp"
#include "combine.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <algorithm>

namespace dune3d {

static constexpr const char *hollow_name = "Hollow";

// the body's groups, without a rounded-edges step at the end: hollowing goes before
// it, so the rim gets rounded too
static std::vector<const Group *> body_groups_of(Document &doc, const UUID &body)
{
    const auto rounded = find_round_edges(doc, body);
    for (const auto &bg : doc.get_groups_by_body()) {
        if (bg.get_group().m_uuid == body) {
            auto groups = bg.groups;
            if (rounded && groups.size() > 1 && groups.back()->m_uuid == rounded->m_uuid)
                groups.pop_back();
            return groups;
        }
    }
    return {};
}

GroupShell *find_hollow(Document &doc, const UUID &body)
{
    const auto groups = body_groups_of(doc, body);
    if (groups.empty() || groups.back()->get_type() != Group::Type::SHELL)
        return nullptr;
    return &doc.get_group<GroupShell>(groups.back()->m_uuid);
}

GroupShell *set_hollow(Document &doc, const UUID &body, double thickness, GroupShell::Open open)
{
    auto existing = find_hollow(doc, body);
    if (thickness <= 0) {
        if (existing) {
            ItemsToDelete items;
            items.groups.insert(existing->m_uuid);
            // with nothing after it in the body, combinations may be using it
            repoint_combinations(doc, existing->m_uuid, doc.get_group_rel(existing->m_uuid, -1));
            doc.set_group_generate_pending(doc.get_group_rel(existing->m_uuid, -1));
            items.append(doc.get_additional_items_to_delete(items));
            doc.delete_items(items);
        }
        return nullptr;
    }
    if (existing) {
        existing->m_thickness = thickness;
        existing->m_open = open;
        doc.set_group_update_solid_model_pending(existing->m_uuid);
        return existing;
    }
    const auto groups = body_groups_of(doc, body);
    if (groups.empty())
        return nullptr;
    const auto &last = *groups.back();
    if (!SolidModel::get_last_solid_model(doc, last, SolidModel::IncludeGroup::YES))
        return nullptr;
    auto &shell = doc.insert_group<GroupShell>(UUID::random(), last.m_uuid);
    shell.m_name = hollow_name;
    shell.m_thickness = thickness;
    shell.m_open = open;
    doc.set_group_generate_pending(shell.m_uuid);
    repoint_combinations(doc, last.m_uuid, shell.m_uuid); // when not rounded, it's the new last step
    return &shell;
}

// the step worked: there's a solid, and a valid one (OCCT can make broken ones without saying so)
static bool works(const SolidModel *sm)
{
    auto occ = dynamic_cast<const SolidModelOcc *>(sm);
    if (!occ || occ->m_shape_acc.IsNull())
        return false;
    return BRepCheck_Analyzer(occ->m_shape_acc).IsValid();
}

static double volume(const SolidModel *sm)
{
    auto occ = dynamic_cast<const SolidModelOcc *>(sm);
    if (!occ || occ->m_shape_acc.IsNull())
        return 0;
    GProp_GProps props;
    BRepGProp::VolumeProperties(occ->m_shape_acc, props);
    return props.Mass();
}

double get_max_round_edges(Document &doc, const UUID &body)
{
    if (auto shell = find_hollow(doc, body))
        return std::floor(shell->m_thickness * 4.5) / 10;
    return 1e9;
}

bool try_round_edges(Document &doc, const UUID &body, double radius, double fallback)
{
    radius = std::min(radius, get_max_round_edges(doc, body));
    auto fillet = set_round_edges(doc, body, radius);
    doc.update_pending();
    if (radius <= 0 || (fillet && works(fillet->get_solid_model())))
        return true;
    set_round_edges(doc, body, std::min(fallback, radius));
    doc.update_pending();
    return false;
}

bool try_hollow(Document &doc, const UUID &body, double thickness, GroupShell::Open open, double fallback)
{
    auto shell = set_hollow(doc, body, thickness, open);
    // thinner walls take less rounding
    if (auto fillet = find_round_edges(doc, body); fillet && thickness > 0)
        fillet->m_radius = std::min(fillet->m_radius, std::floor(thickness * 4.5) / 10);
    doc.update_pending();
    if (thickness <= 0)
        return true;
    if (shell && works(shell->get_solid_model())) {
        // and it took something away: walls that meet in the middle can leave it as it was, or worse
        const auto before = SolidModel::get_last_solid_model(doc, doc.get_group(doc.get_group_rel(shell->m_uuid, -1)),
                                                             SolidModel::IncludeGroup::YES);
        const auto v = volume(shell->get_solid_model()), v0 = volume(before);
        if (v > 0 && (v0 <= 0 || v < v0 * .999))
            return true;
    }
    set_hollow(doc, body, fallback, open);
    doc.update_pending();
    return false;
}

} // namespace dune3d

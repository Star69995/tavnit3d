#include "object_dimensions.hpp"
#include "document.hpp"
#include "group/group.hpp"
#include "group/group_extrude.hpp"
#include "constraint/constraint.hpp"
#include "constraint/iconstraint_datum.hpp"
#include "constraint/constraint_point_distance.hpp"
#include "entity/entity.hpp"
#include <ranges>
#include <algorithm>

namespace dune3d {

namespace {
// friendlier names for the dimensions the ready-made shapes use
struct DatumInfo {
    std::string label;
    double scale = 1; // shown value = datum * scale
};

int label_order(const std::string &label);

DatumInfo datum_info(const Document &doc, const Constraint &co, const IConstraintDatum &datum, const Group &group,
                     bool body_is_revolved)
{
    using T = Constraint::Type;
    bool from_origin = false;   // horizontal/vertical distance from a workplane's origin: a position
    bool on_own_plane = false;  // on an extra plane made in the same group: an upright profile
    if (auto pd = dynamic_cast<const ConstraintPointDistanceBase *>(&co)) {
        auto en = doc.get_entity_ptr(pd->m_entity1.entity);
        from_origin = en && en->get_type() == Entity::Type::WORKPLANE;
        if (auto wp = doc.get_entity_ptr(pd->m_wrkpl))
            // not the one the whole shape stands on (a raised part's own plane): an extra one
            on_own_plane = wp->m_group == co.m_group && doc.get_group(co.m_group).m_active_wrkpl != wp->m_uuid;
    }
    switch (co.get_type()) {
    case T::POINT_DISTANCE_HORIZONTAL:
        if (from_origin)
            return {"X"};
        if (on_own_plane)
            return {body_is_revolved ? "Radius" : "Width"};
        return {"Width"};
    case T::POINT_DISTANCE_VERTICAL:
        if (from_origin)
            return {"Y"};
        if (on_own_plane)
            return {"Height"};
        return {"Depth"};
    case T::DIAMETER:
        return {"Diameter"};
    case T::RADIUS:
        return {"Radius"};
    case T::POINT_DISTANCE:
        if (auto ex = dynamic_cast<const GroupExtrude *>(&group)) {
            // a symmetric extrusion goes both ways: show the whole thickness
            if (ex->m_mode == GroupExtrude::Mode::OFFSET_SYMMETRIC)
                return {"Depth", 2};
            return {"Height"};
        }
        break;
    default:;
    }
    return {datum.get_datum_name()};
}

} // namespace

std::vector<ObjectDimension> get_object_dimensions(const Document &doc, const UUID &body_uu)
{
    const auto body_groups = doc.get_groups_by_body();
    auto it = std::ranges::find_if(body_groups, [&body_uu](const auto &bg) { return bg.get_group().m_uuid == body_uu; });
    if (it == body_groups.end())
        return {};
    const auto &groups = it->groups;
    const bool revolved = std::ranges::any_of(groups, [](auto gr) { return gr->get_type() == Group::Type::REVOLVE; });
    std::vector<ObjectDimension> dims;
    for (auto gr : groups) {
        for (auto &[uu, co] : doc.m_constraints) {
            if (co->m_group != gr->m_uuid)
                continue;
            auto datum = dynamic_cast<const IConstraintDatum *>(co.get());
            if (!datum || datum->is_measurement())
                continue;
            const auto info = datum_info(doc, *co, *datum, *gr, revolved);
            dims.push_back({info.label, uu, gr->m_uuid, info.scale, datum->get_datum() * info.scale});
        }
    }
    // two diameters (a tube): outer and inner
    {
        std::vector<ObjectDimension *> diameters;
        for (auto &d : dims) {
            if (d.label == "Diameter")
                diameters.push_back(&d);
        }
        if (diameters.size() == 2) {
            std::ranges::sort(diameters, {}, [](auto d) { return -d->value; });
            diameters.at(0)->label = "Outer diameter";
            diameters.at(1)->label = "Inner diameter";
        }
    }
    std::ranges::stable_sort(dims, {}, [](const ObjectDimension &d) { return label_order(d.label); });
    return dims;
}

namespace {

int label_order(const std::string &label)
{
    static const std::vector<std::string> order = {"Width",          "Depth",          "Height", "Diameter",
                                                   "Outer diameter", "Inner diameter", "Radius", "X",
                                                   "Y"};
    auto it = std::ranges::find(order, label);
    return it - order.begin();
}
} // namespace

} // namespace dune3d

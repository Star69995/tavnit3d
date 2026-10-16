#include "import_objects.hpp"
#include "document.hpp"
#include "material.hpp"
#include "primitives.hpp"
#include "group/group_sketch.hpp"
#include "group/group_reference.hpp"
#include "entity/entity_step.hpp"
#include "import_step/step_import_manager.hpp"
#include "import_step/shapes.hpp"
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

namespace dune3d {

std::vector<UUID> import_objects(Document &doc, const UUID &after, const std::filesystem::path &file,
                                 const std::filesystem::path &containing_dir)
{
    auto imported = STEPImportManager::get().import_step(file);
    const auto shapes = imported->get_shapes();
    if (!shapes || shapes->parts.empty())
        return {};

    Bnd_Box box;
    for (const auto &part : shapes->parts) {
        for (const auto &sh : part.shapes)
            BRepBndLib::Add(sh, box, true);
    }
    if (box.IsVoid())
        return {};
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    // its bottom on the XY plane, next to the other objects
    const auto spot = find_free_spot(doc, after, std::max(x1 - x0, y1 - y0));
    const glm::dvec3 origin{std::round(spot.x - (x0 + x1) / 2), std::round(spot.y - (y0 + y1) / 2), -z0};

    std::filesystem::path stored = file;
    if (!containing_dir.empty()) {
        std::error_code ec;
        auto rel = std::filesystem::relative(file, containing_dir, ec);
        if (!ec && !rel.empty() && *rel.begin() != "..")
            stored = rel;
    }

    std::vector<UUID> bodies;
    UUID prev = after;
    const auto wrkpl = doc.get_reference_group().get_workplane_xy_uuid();
    for (unsigned int i = 0; i < shapes->parts.size(); i++) {
        const auto &part = shapes->parts.at(i);
        auto &sketch = doc.insert_group<GroupSketch>(UUID::random(), prev);
        sketch.m_name = doc.find_next_group_name(Group::Type::SKETCH);
        sketch.m_active_wrkpl = wrkpl;
        auto &body = sketch.m_body.emplace();
        body.m_name = part.name;
        if (part.color)
            body.m_color = Color(part.color->at(0), part.color->at(1), part.color->at(2));
        if (part.has_material)
            body.m_material = material_from_pbr({part.metallic, part.roughness, part.alpha});

        auto &en = doc.add_entity<EntitySTEP>(UUID::random());
        en.m_group = sketch.m_uuid;
        en.m_path = stored;
        en.m_origin = origin;
        en.m_include_in_solid_model = true;
        en.m_part = i;
        en.m_imported = imported;

        bodies.push_back(sketch.m_uuid);
        prev = sketch.m_uuid;
    }
    doc.set_group_generate_pending(bodies.front());
    return bodies;
}

} // namespace dune3d

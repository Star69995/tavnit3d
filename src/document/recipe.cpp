#include "recipe.hpp"
#include "document.hpp"
#include "primitives.hpp"
#include "combine.hpp"
#include "hollow.hpp"
#include "thread.hpp"
#include "round_edges.hpp"
#include "material.hpp"
#include "group/group.hpp"
#include "nlohmann/json.hpp"
#include <glm/gtc/quaternion.hpp>
#include <map>

namespace dune3d {

static const char *schema_text = R"JSON({
  "type": "object",
  "properties": {
    "objects": {
      "type": "array",
      "description": "The objects to make. Millimetres; z is up, the floor is z = 0.",
      "items": {
        "type": "object",
        "properties": {
          "id": {"type": "string", "description": "Short name to refer to it in combine"},
          "name": {"type": "string", "description": "Name shown to the user"},
          "shape": {"type": "string", "enum": ["box", "cylinder", "sphere", "cone", "tube", "torus", "wedge", "revolve", "extrude"],
            "description": "box: width (x), depth (y), height (z). cylinder: diameter, height. sphere: diameter. cone: diameter at the bottom, height, pointed. tube: diameter, inner_diameter, height. torus: ring_diameter (through the middle of the ring), tube_diameter; lying flat. wedge: width, depth, height, sloping down towards +x. revolve: profile turned around the vertical axis. extrude: outline pushed up by height."},
          "width": {"type": "number"},
          "depth": {"type": "number"},
          "height": {"type": "number"},
          "diameter": {"type": "number"},
          "inner_diameter": {"type": "number"},
          "ring_diameter": {"type": "number"},
          "tube_diameter": {"type": "number"},
          "profile": {"type": "array", "items": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2},
            "description": "revolve: [distance from the axis, height] points of the outer silhouette from the bottom to the top, e.g. a bowl [[0,0],[30,0],[45,12],[55,35],[0,35]]. Solid inside - use hollow for a bowl, cup or vase."},
          "outline": {"type": "array", "items": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2},
            "description": "extrude: [x, y] points around the object's centre, in order, not repeating the first"},
          "smooth": {"type": "boolean", "description": "revolve/extrude: a smooth curve through the points instead of straight lines"},
          "position": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3,
            "description": "[x, y, z] of the middle of the object's bottom (for revolve: the axis at the bottom). Default [0, 0, 0]."},
          "rotation": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3,
            "description": "Degrees about x, then y, then z, turning the object around its bottom middle. E.g. [90, 0, 0] stands a torus up like a ring/handle."},
          "color": {"type": "string", "description": "#rrggbb"},
          "material": {"type": "string", "enum": ["satin", "matte", "glossy", "metal", "glass"]},
          "hollow": {"type": "object", "properties": {
              "thickness": {"type": "number", "description": "wall thickness"},
              "open": {"type": "string", "enum": ["top", "bottom", "none"]}},
            "required": ["thickness"]},
          "thread": {"type": "object", "description": "a helical screw thread round an upright cylinder (diameter = the root, i.e. the core), from the bottom up",
            "properties": {
              "pitch": {"type": "number"},
              "depth": {"type": "number", "description": "ridge height above the cylinder"},
              "length": {"type": "number"}},
            "required": ["pitch", "depth", "length"]},
          "round_edges": {"type": "number", "description": "radius to round all edges with; less than half the wall thickness when hollow"}
        },
        "required": ["id", "shape"]
      }
    },
    "combine": {
      "type": "array",
      "description": "Applied in order after making the objects; the result keeps the target's id",
      "items": {
        "type": "object",
        "properties": {
          "op": {"type": "string", "enum": ["join", "subtract", "intersect"]},
          "target": {"type": "string"},
          "tool": {"type": "string", "description": "joined to / cut out of the target"}
        },
        "required": ["op", "target", "tool"]
      }
    }
  },
  "required": ["objects"]
})JSON";

const json &get_recipe_schema()
{
    static const json schema = json::parse(schema_text);
    return schema;
}

namespace {
double num(const json &o, const char *key, double def)
{
    if (o.contains(key) && o.at(key).is_number())
        return o.at(key).get<double>();
    return def;
}

std::vector<glm::dvec2> points(const json &o, const char *key)
{
    std::vector<glm::dvec2> r;
    if (o.contains(key) && o.at(key).is_array()) {
        for (const auto &p : o.at(key)) {
            if (p.is_array() && p.size() >= 2 && p.at(0).is_number() && p.at(1).is_number())
                r.emplace_back(p.at(0).get<double>(), p.at(1).get<double>());
        }
    }
    return r;
}

std::optional<Color> parse_color(const std::string &s)
{
    if (s.size() != 7 || s.front() != '#')
        return {};
    try {
        const auto v = std::stoul(s.substr(1), nullptr, 16);
        return Color::new_from_int((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
    }
    catch (...) {
        return {};
    }
}

UUID body_of(Document &doc, const Group &group)
{
    return group.find_body(doc).group.m_uuid;
}
} // namespace

RecipeResult build_recipe(Document &doc, const UUID &after, const json &recipe, const glm::dvec2 &origin)
{
    RecipeResult result;
    if (!recipe.contains("objects") || !recipe.at("objects").is_array())
        throw std::runtime_error("the recipe has no objects");

    std::map<std::string, UUID> ids; // to bodies
    UUID last = after;
    for (const auto &o : recipe.at("objects")) {
        const auto shape = o.value("shape", "box");
        const auto id = o.value("id", "object" + std::to_string(ids.size() + 1));

        glm::dvec3 position{0, 0, 0};
        if (o.contains("position") && o.at("position").is_array() && o.at("position").size() == 3)
            position = {o.at("position").at(0).get<double>(), o.at("position").at(1).get<double>(),
                        o.at("position").at(2).get<double>()};
        position += glm::dvec3(origin, 0);
        glm::dvec3 rotation{0, 0, 0};
        if (o.contains("rotation") && o.at("rotation").is_array() && o.at("rotation").size() == 3)
            rotation = {o.at("rotation").at(0).get<double>(), o.at("rotation").at(1).get<double>(),
                        o.at("rotation").at(2).get<double>()};
        // standing on the floor unturned: placed like the other shapes, by X and Y dimensions
        std::optional<Placement> placement;
        if (position.z != 0 || glm::length(rotation) > 1e-9) {
            Placement pl;
            pl.origin = position;
            pl.rotation = glm::angleAxis(glm::radians(rotation.z), glm::dvec3(0, 0, 1))
                          * glm::angleAxis(glm::radians(rotation.y), glm::dvec3(0, 1, 0))
                          * glm::angleAxis(glm::radians(rotation.x), glm::dvec3(1, 0, 0));
            placement = pl;
        }
        const glm::dvec2 pos{position.x, position.y};

        Group *group = nullptr;
        try {
            if (shape == "revolve") {
                group = &add_revolved_profile(doc, last, points(o, "profile"), o.value("smooth", false), pos,
                                              placement);
            }
            else if (shape == "extrude") {
                group = &add_extruded_outline(doc, last, points(o, "outline"), num(o, "height", 10),
                                              o.value("smooth", false), pos, placement);
            }
            else {
                static const std::map<std::string, Primitive> prims = {
                        {"box", Primitive::BOX},   {"cylinder", Primitive::CYLINDER}, {"sphere", Primitive::SPHERE},
                        {"cone", Primitive::CONE}, {"tube", Primitive::TUBE},         {"torus", Primitive::TORUS},
                        {"wedge", Primitive::WEDGE},
                };
                if (!prims.contains(shape)) {
                    result.warnings.push_back("unknown shape " + shape);
                    continue;
                }
                PrimitiveParams p;
                p.width = num(o, "width", p.width);
                p.depth = num(o, "depth", p.depth);
                p.height = num(o, "height", p.height);
                p.diameter = num(o, "diameter", p.diameter);
                p.inner_diameter = num(o, "inner_diameter", p.diameter * .6);
                p.ring_diameter = num(o, "ring_diameter", p.ring_diameter);
                p.tube_diameter = num(o, "tube_diameter", p.tube_diameter);
                group = &add_primitive(doc, last, prims.at(shape), pos, p, placement);
            }
        }
        catch (const std::exception &e) {
            result.warnings.push_back(id + ": " + e.what());
            continue;
        }
        const auto body = body_of(doc, *group);
        auto &b = *doc.get_group(body).m_body;
        if (o.contains("name") && o.at("name").is_string())
            b.m_name = o.at("name").get<std::string>();
        if (o.contains("color") && o.at("color").is_string())
            b.m_color = parse_color(o.at("color").get<std::string>());
        if (o.contains("material") && o.at("material").is_string()) {
            for (const auto m : get_all_materials()) {
                std::string label = get_material_label(m);
                std::ranges::transform(label, label.begin(), [](unsigned char c) { return std::tolower(c); });
                if (label == o.at("material").get<std::string>())
                    b.m_material = m;
            }
        }
        // hollowing and rounding work on the solid, so that's needed first
        doc.update_pending();
        if (o.contains("hollow") && o.at("hollow").is_object()) {
            const auto &h = o.at("hollow");
            auto open = GroupShell::Open::TOP;
            const auto o_str = h.value("open", "top");
            if (o_str == "bottom")
                open = GroupShell::Open::BOTTOM;
            else if (o_str == "none")
                open = GroupShell::Open::NONE;
            if (set_hollow(doc, body, num(h, "thickness", 2), open))
                doc.update_pending();
        }
        if (o.contains("thread") && o.at("thread").is_object()) {
            const auto &t = o.at("thread");
            // the root radius is read off the solid itself: the cylinder it is
            const double radius = num(o, "diameter", num(t, "root_diameter", 0)) / 2;
            if (radius > 0 && set_thread(doc, body, radius, num(t, "pitch", 1), num(t, "depth", .6), num(t, "length", 10)))
                doc.update_pending();
        }
        if (const auto r = num(o, "round_edges", 0); r > 0) {
            if (set_round_edges(doc, body, r))
                doc.update_pending();
        }
        ids[id] = body;
        result.bodies.push_back(body);
        last = doc.get_groups_sorted().back()->m_uuid;
    }

    if (recipe.contains("combine") && recipe.at("combine").is_array()) {
        for (const auto &c : recipe.at("combine")) {
            const auto target = c.value("target", ""), tool = c.value("tool", ""), op_str = c.value("op", "join");
            if (!ids.contains(target) || !ids.contains(tool) || target == tool) {
                result.warnings.push_back("can't combine " + target + " and " + tool);
                continue;
            }
            auto op = IGroupSolidModel::Operation::UNION;
            if (op_str == "subtract")
                op = IGroupSolidModel::Operation::DIFFERENCE;
            else if (op_str == "intersect")
                op = IGroupSolidModel::Operation::INTERSECTION;
            const auto a = ids.at(target), b = ids.at(tool);
            auto &group = add_combination(doc, a, b, op);
            doc.update_pending();
            const auto combined = body_of(doc, group);
            // it's still the target, as far as whoever wrote the recipe is concerned
            doc.get_group(combined).m_body->m_name = doc.get_group(a).m_body->m_name;
            result.hidden.push_back(a);
            result.hidden.push_back(b);
            result.bodies.push_back(combined);
            ids[target] = combined;
        }
    }
    return result;
}

} // namespace dune3d

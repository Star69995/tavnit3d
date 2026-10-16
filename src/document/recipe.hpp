#pragma once
#include "util/uuid.hpp"
#include "nlohmann/json_fwd.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace dune3d {
using json = nlohmann::json;
class Document;

// A "recipe" describes objects in plain terms (shape, sizes in mm, where they
// stand, colour, material, hollowing, rounding, and objects joined or cut out
// of each other), for example as written by an AI from a description. They're
// built from the usual parametric building blocks, so they can be edited
// afterwards like any other object. get_recipe_schema() is the JSON schema.
struct RecipeResult {
    std::vector<UUID> bodies; // all new ones
    std::vector<UUID> hidden; // used up by combining; best hidden
    std::vector<std::string> warnings;
};

// Builds the recipe's objects after group `after`, placed relative to `origin`
// (so they don't land inside what's there). Rebuilds the document as needed.
RecipeResult build_recipe(Document &doc, const UUID &after, const json &recipe, const glm::dvec2 &origin = {0, 0});

const json &get_recipe_schema();

} // namespace dune3d

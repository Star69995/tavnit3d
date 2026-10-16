#pragma once
#include "nlohmann/json.hpp"
#include <string>

namespace dune3d {
using json = nlohmann::json;

// Asks Claude to turn a description ("a bowl", "a mug with a handle") into a
// recipe (see recipe.hpp). Blocks for as long as that takes, so run it in a
// thread. DUNE3D_CLAUDE_URL overrides where the request goes (for testing).
struct AiRecipeAnswer {
    json recipe;       // empty if it didn't work
    std::string error; // what went wrong, for the user
};

AiRecipeAnswer ask_for_recipe(const std::string &api_key, const std::string &description);

// what's sent, for tests
json make_recipe_request(const std::string &description);

} // namespace dune3d

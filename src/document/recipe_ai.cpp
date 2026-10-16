#include "recipe_ai.hpp"
#include "recipe.hpp"
#include "util/http.hpp"
#include <cstdlib>

namespace dune3d {

static const char *model = "claude-opus-5-5";

static const char *system_prompt = R"(You design 3D objects for Tavnit 3D, a simple CAD program, from a short
description by its user. Answer with a recipe: the objects that make up the thing, as shapes the program
builds and the user can then edit (sizes, position, colour, material).

How the program sees things:
- Millimetres. z is up; the floor (a table top) is z = 0, and things normally stand on it.
- Each object stands on its bottom middle, at "position". A revolve's axis runs straight up through it.
- "rotation" turns an object around its bottom middle: degrees about x, then y, then z.
- Combining makes a new object from two: join, or subtract the tool from the target (holes, cut-outs).
- "hollow" makes walls of the given thickness, inwards, open at the top by default: bowls, cups, vases,
  boxes, planters. A revolve is solid, so vessels need it.
- "round_edges" rounds every edge; keep it below half the wall thickness of hollow objects.

Good habits:
- Realistic sizes for real things (a mug is about 80 mm across and 95 mm tall, a cereal bowl about 150 mm
  across and 60 mm tall) unless the user gives sizes.
- Use the fewest objects that make it look right. Revolve with smooth = true for rounded vessels and turned
  shapes; extrude for flat outlines (letters, brackets, plates); the ready-made shapes for the rest.
- Make parts that touch actually overlap a little, then join them, so the result is one solid.
- Give the objects short names in the user's language and fitting colours (#rrggbb) and materials.
- If the description is vague, pick a nice, typical version of it.)";

// The recipe schema as structured outputs want it: every object closed, and
// no limits on array lengths
static json strict_schema(json s)
{
    if (s.is_object()) {
        s.erase("minItems");
        s.erase("maxItems");
        if (s.value("type", "") == "object")
            s["additionalProperties"] = false;
        for (auto &[k, v] : s.items())
            v = strict_schema(v);
    }
    else if (s.is_array()) {
        for (auto &v : s)
            v = strict_schema(v);
    }
    return s;
}

json make_recipe_request(const std::string &description)
{
    return json{
            {"model", model},
            {"max_tokens", 16000},
            {"system", system_prompt},
            {"messages", json::array({{{"role", "user"}, {"content", description}}})},
            {"output_config",
             {{"effort", "high"}, {"format", {{"type", "json_schema"}, {"schema", strict_schema(get_recipe_schema())}}}}},
            // if a safety check declines it, let a model that would answer take over
            {"fallbacks", "default"},
    };
}

AiRecipeAnswer ask_for_recipe(const std::string &api_key, const std::string &description)
{
    AiRecipeAnswer answer;
    std::string url = "https://api.anthropic.com/v1/messages";
    if (const auto env = std::getenv("DUNE3D_CLAUDE_URL"); env && *env)
        url = env;
    HttpResponse resp;
    try {
        resp = http_post(url,
                         {
                                 {"Content-Type", "application/json"},
                                 {"x-api-key", api_key},
                                 {"anthropic-version", "2023-06-01"},
                                 {"anthropic-beta", "server-side-fallback-2026-07-01"},
                         },
                         make_recipe_request(description).dump(), 300);
    }
    catch (const std::exception &e) {
        answer.error = std::string("Couldn't reach Claude: ") + e.what();
        return answer;
    }
    json j;
    try {
        j = json::parse(resp.body);
    }
    catch (...) {
        answer.error = "Claude's answer didn't make sense (HTTP " + std::to_string(resp.status) + ")";
        return answer;
    }
    if (resp.status != 200) {
        std::string msg;
        if (j.contains("error") && j.at("error").is_object())
            msg = j.at("error").value("message", "");
        if (resp.status == 401)
            answer.error = "The API key wasn't accepted";
        else if (resp.status == 429)
            answer.error = "Too many requests right now, try again in a minute";
        else if (resp.status >= 500)
            answer.error = "Claude is busy or unavailable right now, try again later";
        else
            answer.error = "Claude couldn't take the request";
        if (msg.size())
            answer.error += " (" + msg + ")";
        return answer;
    }
    const auto stop = j.value("stop_reason", "");
    if (stop == "refusal") {
        answer.error = "Claude declined to make that";
        return answer;
    }
    if (stop == "max_tokens") {
        answer.error = "That came out too big - try something simpler";
        return answer;
    }
    std::string text;
    if (j.contains("content")) {
        for (const auto &block : j.at("content")) {
            if (block.value("type", "") == "text")
                text += block.value("text", "");
        }
    }
    try {
        answer.recipe = json::parse(text);
    }
    catch (...) {
        answer.error = "Claude's answer wasn't a usable recipe";
        return answer;
    }
    if (!answer.recipe.contains("objects")) {
        answer.recipe = json();
        answer.error = "Claude's answer had no objects in it";
    }
    return answer;
}

} // namespace dune3d

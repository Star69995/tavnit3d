#include "recipe_ai.hpp"
#include "recipe.hpp"
#include "util/http.hpp"
#include <cstdlib>
#include <stdexcept>

namespace dune3d {

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


const std::vector<AiProvider> &ai_providers()
{
    static const std::vector<AiProvider> providers = {
            {"claude", "Claude (Anthropic)", AiApi::ANTHROPIC, "https://api.anthropic.com/v1/messages", "claude-opus-5-5",
             "https://console.anthropic.com/settings/keys", "ANTHROPIC_API_KEY"},
            {"openai", "ChatGPT (OpenAI)", AiApi::OPENAI, "https://api.openai.com/v1", "gpt-5",
             "https://platform.openai.com/api-keys", "OPENAI_API_KEY"},
            {"gemini", "Gemini (Google)", AiApi::OPENAI, "https://generativelanguage.googleapis.com/v1beta/openai",
             "gemini-2.5-pro", "https://aistudio.google.com/apikey", "GEMINI_API_KEY"},
            {"openrouter", "OpenRouter (many models)", AiApi::OPENAI, "https://openrouter.ai/api/v1",
             "anthropic/claude-sonnet-4.5", "https://openrouter.ai/keys", "OPENROUTER_API_KEY"},
            {"other", "Other (OpenAI-compatible address)", AiApi::OPENAI, "", "", "", ""},
    };
    return providers;
}

const AiProvider &ai_provider(const std::string &id)
{
    for (const auto &p : ai_providers()) {
        if (p.id == id)
            return p;
    }
    return ai_providers().front();
}

static std::string lookup(const std::map<std::string, std::string> &m, const std::string &id)
{
    if (const auto it = m.find(id); it != m.end())
        return it->second;
    return {};
}

std::string AiSettings::get_key() const
{
    if (const auto k = lookup(keys, provider); k.size())
        return k;
    const auto &p = ai_provider(provider);
    if (p.env_var.size()) {
        if (const auto env = std::getenv(p.env_var.c_str()); env && *env)
            return env;
    }
    return {};
}

std::string AiSettings::get_model() const
{
    if (const auto m = lookup(models, provider); m.size())
        return m;
    return ai_provider(provider).model;
}

std::string AiSettings::get_url() const
{
    if (const auto u = lookup(urls, provider); u.size())
        return u;
    return ai_provider(provider).url;
}

static std::string recipe_instructions()
{
    return std::string(system_prompt) + "\n\nAnswer with only a JSON object (no text before or after it, no code "
                                        "fences) that follows this JSON schema:\n"
           + get_recipe_schema().dump();
}

std::string make_chatbot_prompt(const std::string &description)
{
    return recipe_instructions() + "\n\nWhat to make:\n" + description + "\n";
}

AiRecipeAnswer parse_recipe_text(const std::string &text)
{
    AiRecipeAnswer answer;
    // chatbots add chatter and ```json fences; take from the first { to the last }
    const auto first = text.find('{');
    const auto last = text.rfind('}');
    if (first == std::string::npos || last == std::string::npos || last < first) {
        answer.error = "There's no recipe in that text (it should contain a JSON object)";
        return answer;
    }
    try {
        answer.recipe = json::parse(text.substr(first, last - first + 1));
    }
    catch (const std::exception &e) {
        answer.error = std::string("That text isn't a complete recipe (") + e.what() + ")";
        return answer;
    }
    if (!answer.recipe.is_object() || !answer.recipe.contains("objects") || !answer.recipe.at("objects").is_array()) {
        answer.recipe = json();
        answer.error = "The answer had no objects in it";
    }
    return answer;
}

json make_recipe_request(const std::string &description, const std::string &model)
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

json make_openai_request(const std::string &description, const std::string &model)
{
    return json{
            {"model", model},
            {"messages", json::array({{{"role", "system"}, {"content", recipe_instructions()}},
                                      {{"role", "user"}, {"content", description}}})},
            {"response_format", {{"type", "json_object"}}},
    };
}

// "Claude couldn't ..." in the user's words, whichever service it was
static std::string error_message(const std::string &who, int status, const json &j)
{
    std::string msg;
    if (j.is_object() && j.contains("error")) {
        const auto &e = j.at("error");
        if (e.is_object() && e.contains("message") && e.at("message").is_string())
            msg = e.at("message").get<std::string>();
        else if (e.is_string())
            msg = e.get<std::string>();
    }
    else if (j.is_array() && j.size() && j.at(0).is_object() && j.at(0).contains("error")) {
        const auto &e = j.at(0).at("error");
        if (e.is_object() && e.contains("message") && e.at("message").is_string())
            msg = e.at("message").get<std::string>();
    }
    std::string r;
    if (status == 401 || status == 403)
        r = "The API key wasn't accepted";
    else if (status == 429)
        r = "Too many requests right now, try again in a minute";
    else if (status >= 500)
        r = who + " is busy or unavailable right now, try again later";
    else
        r = who + " couldn't take the request";
    if (msg.size())
        r += " (" + msg + ")";
    return r;
}

AiRecipeAnswer ask_for_recipe(const AiSettings &settings, const std::string &description)
{
    AiRecipeAnswer answer;
    const auto &provider = ai_provider(settings.provider);
    const auto who = provider.id == "claude" ? std::string("Claude") : provider.label;
    const auto model = settings.get_model();
    auto url = settings.get_url();
    if (provider.id == "claude") {
        if (const auto env = std::getenv("DUNE3D_CLAUDE_URL"); env && *env)
            url = env;
    }
    if (url.empty()) {
        answer.error = "Fill in the address of the service first";
        return answer;
    }
    if (model.empty()) {
        answer.error = "Fill in the model name first";
        return answer;
    }
    const bool claude = provider.api == AiApi::ANTHROPIC;
    if (!claude) {
        while (url.size() && url.back() == '/')
            url.pop_back();
        if (!url.ends_with("/chat/completions"))
            url += "/chat/completions";
    }
    HttpHeaders headers = {{"Content-Type", "application/json"}};
    if (claude) {
        headers.emplace_back("x-api-key", settings.get_key());
        headers.emplace_back("anthropic-version", "2023-06-01");
        headers.emplace_back("anthropic-beta", "server-side-fallback-2026-07-01");
    }
    else if (const auto key = settings.get_key(); key.size()) {
        headers.emplace_back("Authorization", "Bearer " + key);
    }
    HttpResponse resp;
    try {
        const auto body = claude ? make_recipe_request(description, model) : make_openai_request(description, model);
        resp = http_post(url, headers, body.dump(), 300);
    }
    catch (const std::exception &e) {
        answer.error = "Couldn't reach " + who + ": " + e.what();
        return answer;
    }
    json j;
    try {
        j = json::parse(resp.body);
    }
    catch (...) {
        answer.error = who + "'s answer didn't make sense (HTTP " + std::to_string(resp.status) + ")";
        return answer;
    }
    if (resp.status != 200) {
        answer.error = error_message(who, resp.status, j);
        return answer;
    }
    std::string text;
    if (claude) {
        const auto stop = j.value("stop_reason", "");
        if (stop == "refusal") {
            answer.error = "Claude declined to make that";
            return answer;
        }
        if (stop == "max_tokens") {
            answer.error = "That came out too big - try something simpler";
            return answer;
        }
        if (j.contains("content")) {
            for (const auto &block : j.at("content")) {
                if (block.value("type", "") == "text")
                    text += block.value("text", "");
            }
        }
    }
    else {
        try {
            const auto &choice = j.at("choices").at(0);
            if (choice.value("finish_reason", "") == "length") {
                answer.error = "That came out too big - try something simpler";
                return answer;
            }
            const auto &content = choice.at("message").at("content");
            if (content.is_string())
                text = content.get<std::string>();
        }
        catch (...) {
            answer.error = who + "'s answer wasn't in the expected form";
            return answer;
        }
    }
    answer = parse_recipe_text(text);
    if (answer.error.size())
        answer.error = who + "'s answer wasn't a usable recipe (" + answer.error + ")";
    return answer;
}

} // namespace dune3d

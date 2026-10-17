// Making shapes from descriptions: reading a chatbot's answer, the prompt for
// one, and the requests for the different AI services (no network).
#include "document/recipe_ai.hpp"
#include "preferences/preferences.hpp"
#include <iostream>

using namespace dune3d;

const Preferences &Preferences::get()
{
    static Preferences the_preferences;
    return the_preferences;
}

static int failures = 0;

static void check(bool ok, const std::string &what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok)
        failures++;
}

int main()
{
    const std::string recipe = R"({"objects": [{"id": "a", "shape": "box", "size": [10, 10, 10]}]})";
    check(parse_recipe_text(recipe).error.empty(), "plain JSON");
    const auto fenced = parse_recipe_text("Sure! Here you go:\n```json\n" + recipe + "\n```\nEnjoy.");
    check(fenced.error.empty() && fenced.recipe.at("objects").size() == 1, "JSON in code fences with chatter");
    check(parse_recipe_text("no json here").error.size(), "text without JSON is an error");
    check(parse_recipe_text("{\"objects\": [").error.size(), "cut-off JSON is an error");
    check(parse_recipe_text("{\"things\": []}").error.size(), "JSON without objects is an error");

    const auto prompt = make_chatbot_prompt("a mug");
    check(prompt.find("a mug") != std::string::npos, "prompt has the description");
    check(prompt.find("\"objects\"") != std::string::npos, "prompt has the schema");
    check(prompt.find("only a JSON object") != std::string::npos, "prompt asks for JSON only");

    const auto oa = make_openai_request("a mug", "some-model");
    check(oa.at("model") == "some-model" && oa.at("messages").size() == 2, "OpenAI-style request");
    check(oa.at("messages").at(0).at("content").get<std::string>().find("\"objects\"") != std::string::npos,
          "schema goes to services without structured outputs");
    const auto cl = make_recipe_request("a mug");
    check(cl.contains("output_config") && cl.at("model") == "claude-opus-5-5", "Claude request");

    AiSettings s;
    check(s.get_model() == "claude-opus-5-5", "default model");
    s.provider = "openai";
    s.keys["claude"] = "sk-ant";
    check(s.get_key() != "sk-ant", "a key is only used for its own service");
    s.keys["openai"] = "sk-x";
    s.models["openai"] = "gpt-x";
    check(s.get_key() == "sk-x" && s.get_model() == "gpt-x", "per-service key and model");
    check(ai_provider("nonsense").id == "claude", "unknown service falls back to the first");

    std::cout << (failures ? "FAILED" : "all ok") << std::endl;
    return failures ? 1 : 0;
}

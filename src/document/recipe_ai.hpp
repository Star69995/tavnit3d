#pragma once
#include "nlohmann/json.hpp"
#include <map>
#include <string>
#include <vector>

namespace dune3d {
using json = nlohmann::json;

// Asks an AI service to turn a description ("a bowl", "a mug with a handle")
// into a recipe (see recipe.hpp). Blocks for as long as that takes, so run it
// in a thread. Any chatbot works, too, without a key: make_chatbot_prompt()
// gives the text to paste into one, parse_recipe_text() reads what it answers.
struct AiRecipeAnswer {
    json recipe;       // empty if it didn't work
    std::string error; // what went wrong, for the user
};

enum class AiApi {
    ANTHROPIC, // Claude's own API
    OPENAI,    // chat completions, which most others (Gemini, OpenRouter, Groq, Ollama, ...) speak too
};

struct AiProvider {
    std::string id; // stored in the settings
    std::string label;
    AiApi api;
    std::string url;   // base url (OpenAI style: without /chat/completions); empty = user fills it in
    std::string model; // a good default
    std::string key_page;
    std::string env_var; // where a key may be found if none was typed in
};

const std::vector<AiProvider> &ai_providers();
const AiProvider &ai_provider(const std::string &id); // the first one if unknown

// What the user chose; stored in the settings
struct AiSettings {
    std::string provider = "claude";
    std::map<std::string, std::string> keys; // per provider id, so one's key never goes to another
    std::map<std::string, std::string> models;
    std::map<std::string, std::string> urls;

    std::string get_key() const; // typed in, else the provider's environment variable
    std::string get_model() const;
    std::string get_url() const;
};

AiRecipeAnswer ask_for_recipe(const AiSettings &settings, const std::string &description);

// For any chatbot: instructions, the recipe format and the description, as one text to paste
std::string make_chatbot_prompt(const std::string &description);

// Reads a chatbot's answer: the JSON in it, with or without ```json fences and chatter around it
AiRecipeAnswer parse_recipe_text(const std::string &text);

// what's sent, for tests
json make_recipe_request(const std::string &description, const std::string &model = "claude-opus-5-5");
json make_openai_request(const std::string &description, const std::string &model);

} // namespace dune3d

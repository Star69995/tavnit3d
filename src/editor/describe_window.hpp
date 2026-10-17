#pragma once
#include <gtkmm.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include "nlohmann/json.hpp"
#include "document/recipe_ai.hpp"

namespace dune3d {

// "Make from a description": the user writes what they want, an AI service
// (any provider with a key) writes a recipe for it and the editor builds that
// (through on_recipe). Without a key, the same prompt can be copied into any
// chatbot and its answer pasted back.
class DescribeWindow : public Gtk::Window {
public:
    using OnRecipe = std::function<bool(const nlohmann::json &recipe, const std::string &description)>;
    DescribeWindow(Gtk::Window &parent, AiSettings &settings, OnRecipe on_recipe);
    ~DescribeWindow() override;

    void set_description(const std::string &text);
    void start();

private:
    AiSettings &m_settings; // remembered in the user's settings
    OnRecipe m_on_recipe;

    Gtk::TextView *m_text = nullptr;
    Gtk::Expander *m_service = nullptr;
    Gtk::DropDown *m_provider = nullptr;
    Gtk::PasswordEntry *m_key_entry = nullptr;
    Gtk::Button *m_get_key = nullptr;
    Gtk::Entry *m_model_entry = nullptr;
    Gtk::Entry *m_url_entry = nullptr;
    Gtk::Box *m_url_box = nullptr;
    Gtk::Button *m_make_button = nullptr;
    Gtk::Spinner *m_spinner = nullptr;
    Gtk::Label *m_status = nullptr;

    bool m_loading = false; // filling the fields, not the user typing
    void load_provider_fields();
    void store_fields();
    std::string get_description();

    void copy_prompt();
    void paste_answer();

    std::thread m_thread;
    std::atomic_bool m_closed = false;
    nlohmann::json m_recipe;
    std::string m_error;
    std::string m_description;
    Glib::Dispatcher m_dispatch_done;
    void finish();
};

} // namespace dune3d

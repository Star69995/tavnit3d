#pragma once
#include <gtkmm.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include "nlohmann/json.hpp"

namespace dune3d {

// "Make from a description": the user writes what they want, Claude writes a
// recipe for it and the editor builds that (through on_recipe).
class DescribeWindow : public Gtk::Window {
public:
    using OnRecipe = std::function<bool(const nlohmann::json &recipe, const std::string &description)>;
    DescribeWindow(Gtk::Window &parent, std::string &api_key, OnRecipe on_recipe);
    ~DescribeWindow() override;

    void set_description(const std::string &text);
    void start();

private:
    std::string &m_api_key; // remembered in the user's settings
    OnRecipe m_on_recipe;

    Gtk::TextView *m_text = nullptr;
    Gtk::Box *m_key_box = nullptr;
    Gtk::PasswordEntry *m_key_entry = nullptr;
    Gtk::Button *m_make_button = nullptr;
    Gtk::Spinner *m_spinner = nullptr;
    Gtk::Label *m_status = nullptr;

    std::string get_key() const;
    void update_key_box();

    std::thread m_thread;
    std::atomic_bool m_closed = false;
    nlohmann::json m_recipe;
    std::string m_error;
    std::string m_description;
    Glib::Dispatcher m_dispatch_done;
    void finish();
};

} // namespace dune3d

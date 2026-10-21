#pragma once
#include <gtkmm.h>
#include "preferences/preferences.hpp"
#include "logger/log_dispatcher.hpp"
#include "util/uuid.hpp"
#include "document/recipe_ai.hpp"
#include <filesystem>

namespace dune3d {

class Dune3DAppWindow;
class Buffer;

class Dune3DApplication : public Gtk::Application {
protected:
    Dune3DApplication();

public:
    static Glib::RefPtr<Dune3DApplication> create();

    Preferences &get_preferences()
    {
        return m_preferences;
    }

    void add_recent_item(const std::filesystem::path &path);
    class UserConfig {
    private:
        friend Dune3DApplication;
        UserConfig() = default;
        void load(const std::filesystem::path &filename);
        void save(const std::filesystem::path &filename);

    public:
        std::map<std::filesystem::path, Glib::DateTime> recent_items;
        struct ExportPaths {
            std::string step;
            std::string stl;
            std::string paths;
            std::string projection;
            std::string gltf;
        };
        // Keys are (document path, group UUID). Group UUID is all zeroes for all-groups exports.
        std::map<std::pair<std::filesystem::path, UUID>, ExportPaths> export_paths;
        bool check_for_updates = true;
        std::filesystem::path blender_path; // for rendering pictures; found by itself if empty
        AiSettings ai;                      // for making shapes from descriptions
        // letters in the 3D view less than this apart are typing (they go to Find), not shortcuts;
        // people type about 150-250 ms apart
        unsigned int typing_threshold_ms = 350;
        // a backup of unsaved work is written this often (0 = off) ...
        unsigned int autosave_minutes = 3;
        bool show_measurements = true;      // size lines on the picked object while the pointer is over it
        bool palette_folded = false;        // the tool palette over the 3D view
        bool show_other_documents = false; // documents hidden from the 3D view still in the side list
        bool one_document_per_screen = false; // each tab shows only its own document
        // for beginners: no documents in the side list, and no tab bar while there is one tab. The
        // installer's choice (install_options.json in the config folder) is taken over once.
        bool simple_ui = false;
        // where movable panels ("palette", "layers") were dragged to, in pixels from the 3D view's
        // top left; x < 0: where they start out. "layers" floats over the view or sits in the sidebar
        struct PanelPlace {
            bool floating = false;
            int x = -1;
            int y = -1;
        };
        std::map<std::string, PanelPlace> panels;
        // ... into "Autosave" in here, which is also where saving starts; Documents/Tavnit 3D if empty
        std::filesystem::path documents_dir;
    };

    UserConfig m_user_config;

    typedef sigc::signal<void()> type_signal_recent_items_changed;
    type_signal_recent_items_changed signal_recent_items_changed()
    {
        return m_signal_recent_items_changed;
    }

    class PreferencesWindow *show_preferences_window(guint32 timestamp = 0);
    class LogWindow *show_log_window(guint32 timestamp = 0);

    std::unique_ptr<const Buffer> m_buffer;

    ~Dune3DApplication();

protected:
    // Override default signal handlers:
    void on_activate() override;
    void on_open(const Gio::Application::type_vec_files &files, const Glib::ustring &hint) override;
    void on_startup() override;
    void on_shutdown() override;

private:
    Dune3DAppWindow *create_appwindow();

    Preferences m_preferences;

    class PreferencesWindow *m_preferences_window = nullptr;

    LogDispatcher m_log_dispatcher;
    class LogWindow *m_log_window = nullptr;

    static std::filesystem::path get_user_config_filename();

    type_signal_recent_items_changed m_signal_recent_items_changed;

    void on_action_about();
};
} // namespace dune3d

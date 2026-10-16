#pragma once
#include <gtkmm.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include "util/blender_render.hpp"

namespace dune3d {

// "Render picture": a photo-like picture of the scene from the current view,
// made by Blender in the background.
class RenderWindow : public Gtk::Window {
public:
    // export_scene(path, aspect): writes the scene as glTF from the view with
    // that width/height ratio, false if there's nothing to render
    using ExportScene = std::function<bool(const std::filesystem::path &, double)>;
    RenderWindow(Gtk::Window &parent, std::filesystem::path &blender_path, double view_aspect,
                 ExportScene export_scene);
    ~RenderWindow() override;

    void start_render();
    bool is_rendering() const
    {
        return m_thread.joinable();
    }
    // the last picture, as shown
    Glib::RefPtr<Gdk::Texture> get_result() const
    {
        return m_result;
    }

private:
    std::filesystem::path &m_blender_path;
    const double m_view_aspect;
    ExportScene m_export_scene;

    Gtk::DropDown *m_size = nullptr;
    Gtk::DropDown *m_quality = nullptr;
    Gtk::DropDown *m_background = nullptr;
    Gtk::Switch *m_floor = nullptr;
    Gtk::Switch *m_fill = nullptr;
    Gtk::Label *m_blender_label = nullptr;
    Gtk::Button *m_get_blender = nullptr;
    Gtk::Button *m_render_button = nullptr;
    Gtk::Button *m_cancel_button = nullptr;
    Gtk::Button *m_save_button = nullptr;
    Gtk::ProgressBar *m_progress = nullptr;
    Gtk::Label *m_status = nullptr;
    Gtk::Picture *m_picture = nullptr;

    void update_blender();
    void choose_blender();
    void save_picture();
    void finish();
    std::pair<int, int> get_size() const;

    BlenderRender m_render;
    std::thread m_thread;
    std::filesystem::path m_dir;
    std::filesystem::path m_png;
    std::atomic_int m_done = 0;
    std::atomic_int m_total = 0;
    std::string m_error;
    Glib::Dispatcher m_dispatch_progress;
    Glib::Dispatcher m_dispatch_done;
    Glib::RefPtr<Gdk::Texture> m_rendered; // as it came out, transparent background
    Glib::RefPtr<Gdk::Texture> m_result;   // on the chosen background
    void show_result();
};

} // namespace dune3d

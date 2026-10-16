#include "editor.hpp"
#include "dune3d_appwindow.hpp"
#include "workspace_browser.hpp"
#include "document/document.hpp"
#include "document/import_objects.hpp"
#include "util/fs_util.hpp"

namespace dune3d {

void Editor::on_import_object()
{
    if (!m_core.has_documents())
        return;
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Import objects");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("3D models (STEP, IGES, glTF, OBJ)");
    for (const auto pat : {"*.step", "*.stp", "*.STEP", "*.STP", "*.iges", "*.igs", "*.IGES", "*.IGS", "*.glb",
                           "*.gltf", "*.GLB", "*.GLTF", "*.obj", "*.OBJ"})
        filter->add_pattern(pat);
    filters->append(filter);
    dialog->set_filters(filters);
    dialog->open(m_win, [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto file = dialog->open_finish(result);
            import_object_file(path_from_string(file->get_path()));
        }
        catch (const Gtk::DialogError &err) {
            // cancelled
        }
        catch (const Glib::Error &err) {
            m_workspace_browser->show_toast(err.what());
        }
    });
}

void Editor::import_object_file(const std::filesystem::path &path)
{
    if (m_core.tool_is_active() || !m_core.has_documents())
        return;
    CanvasUpdater canvas_updater{*this};
    auto &doc_info = m_core.get_current_idocument_info();
    auto &doc = m_core.get_current_document();
    std::vector<UUID> bodies;
    try {
        bodies = import_objects(doc, doc.get_groups_sorted().back()->m_uuid, path,
                                doc_info.has_path() ? doc_info.get_dirname() : std::filesystem::path());
    }
    catch (const std::exception &e) {
        m_workspace_browser->show_toast("Couldn't import that file: " + std::string(e.what()));
        return;
    }
    if (bodies.empty()) {
        m_workspace_browser->show_toast("Found nothing to import in that file");
        return;
    }
    const auto last = doc.get_groups_sorted().back()->m_uuid;
    get_current_document_view().m_current_group = last;
    m_core.set_current_group(last);
    m_core.set_needs_save();
    m_core.rebuild("import objects");
    m_workspace_browser->update_documents(get_current_document_views());
    set_current_group(last);
    m_workspace_browser->show_toast(bodies.size() == 1 ? "Imported 1 object"
                                                       : "Imported " + std::to_string(bodies.size()) + " objects");
    zoom_to_fit();
}

} // namespace dune3d

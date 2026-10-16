#include "editor.hpp"
#include "dune3d_appwindow.hpp"
#include "dune3d_application.hpp"
#include "util/fs_util.hpp"
#include "util/util.hpp"
#include "util/branding.hpp"
#include "nlohmann/json.hpp"
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <algorithm>
#include <iostream>

namespace dune3d {

/*
 * Backups of unsaved work: every few minutes (Preferences in the main menu,
 * 0 = off) each document with unsaved changes is copied to "Autosave" inside
 * the documents folder - the document itself is never touched. A backup goes
 * away when its document is saved or closed on purpose; the ones still there
 * at the next start are from a crash or a power cut, and are offered back.
 *
 * The documents folder is also where saving and opening start. It's
 * Documents/Tavnit 3D unless the user picked another one.
 */

std::filesystem::path Editor::get_documents_dir() const
{
    const auto &chosen = m_win.get_app().m_user_config.documents_dir;
    if (!chosen.empty())
        return chosen;
    const char *docs = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
    return path_from_string(docs ? docs : Glib::get_home_dir()) / app_name;
}

std::filesystem::path Editor::get_autosave_dir() const
{
    return get_documents_dir() / "Autosave";
}

namespace {
std::filesystem::path backup_file(const std::filesystem::path &dir, const UUID &uu)
{
    return dir / path_from_string(std::string(uu) + ".d3ddoc");
}
std::filesystem::path backup_info(const std::filesystem::path &dir, const UUID &uu)
{
    return dir / path_from_string(std::string(uu) + ".json");
}
void remove_quietly(const std::filesystem::path &path)
{
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}
} // namespace

void Editor::autosave_now()
{
    const auto dir = get_autosave_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    for (auto doc : m_core.get_documents()) {
        const auto uu = doc->get_uuid();
        if (!doc->get_needs_save()) {
            // saved since the last backup
            if (std::filesystem::exists(backup_file(dir, uu), ec))
                autosave_remove(uu);
            continue;
        }
        try {
            m_core.write_backup(uu, backup_file(dir, uu));
            json info = {{"name", doc->get_stem()}};
            if (doc->has_path())
                info["original"] = path_to_string(doc->get_path());
            save_json_to_file(backup_info(dir, uu), info);
        }
        catch (const std::exception &e) {
            std::cerr << "autosave failed: " << e.what() << std::endl;
        }
    }
}

void Editor::autosave_remove(const UUID &doc_uu)
{
    const auto dir = get_autosave_dir();
    const auto doc = backup_file(dir, doc_uu);
    remove_quietly(doc);
    remove_quietly(doc.parent_path() / path_from_string(std::string(doc_uu) + ".d3dpic"));
    remove_quietly(backup_info(dir, doc_uu));
}

void Editor::restart_autosave_timer()
{
    m_autosave_connection.disconnect();
    const auto minutes = m_win.get_app().m_user_config.autosave_minutes;
    if (!minutes)
        return;
    m_autosave_connection = Glib::signal_timeout().connect_seconds(
            [this] {
                autosave_now();
                return true;
            },
            minutes * 60);
}

void Editor::offer_recovery()
{
    const auto dir = get_autosave_dir();
    std::vector<std::filesystem::path> found;
    std::error_code ec;
    for (const auto &it : std::filesystem::directory_iterator(dir, ec)) {
        if (it.path().extension() == ".d3ddoc")
            found.push_back(it.path());
    }
    if (found.empty())
        return;
    auto dialog = Gtk::AlertDialog::create("Unsaved work from last time was found");
    dialog->set_detail(std::to_string(found.size())
                       + " backup(s) were made automatically but the work was never saved. Open them now?");
    dialog->set_buttons({"Delete them", "Open them"});
    dialog->set_cancel_button(0);
    dialog->set_default_button(1);
    dialog->choose(m_win, [this, dialog, found, dir](Glib::RefPtr<Gio::AsyncResult> &result) {
        int btn = 0;
        try {
            btn = dialog->choose_finish(result);
        }
        catch (const Glib::Error &) {
            return; // leave them for next time
        }
        for (const auto &file : found) {
            const auto stem = path_to_string(file.stem());
            if (btn == 1) {
                // copy to the documents folder first, so saving the recovered work doesn't touch the backup
                std::string name = "Recovered";
                try {
                    const auto info = load_json_from_file(dir / path_from_string(stem + ".json"));
                    name = "Recovered " + info.value("name", std::string("document"));
                }
                catch (...) {
                }
                auto target = get_documents_dir() / path_from_string(name + ".d3ddoc");
                for (int n = 2; std::filesystem::exists(target); n++)
                    target = get_documents_dir() / path_from_string(name + " " + std::to_string(n) + ".d3ddoc");
                std::error_code ec2;
                std::filesystem::copy_file(file, target, ec2);
                const auto pics = dir / path_from_string(stem + ".d3dpic");
                if (std::filesystem::exists(pics, ec2)) {
                    auto pics_target = target;
                    pics_target.replace_extension(".d3dpic");
                    std::filesystem::copy(pics, pics_target, std::filesystem::copy_options::recursive, ec2);
                }
                if (!ec2)
                    m_win.open_file_view(Gio::File::create_for_path(path_to_string(target)));
            }
            remove_quietly(file);
            remove_quietly(dir / path_from_string(stem + ".d3dpic"));
            remove_quietly(dir / path_from_string(stem + ".json"));
        }
    });
}

void Editor::init_autosave()
{
    {
        std::error_code ec;
        std::filesystem::create_directories(get_documents_dir(), ec);
    }
    auto &config = m_win.get_app().m_user_config;

    m_win.add_action_radio_string(
            "autosave_interval",
            [this](const Glib::ustring &v) {
                auto &cfg = m_win.get_app().m_user_config;
                cfg.autosave_minutes = v == "off" ? 0 : std::stoul(v);
                m_win.lookup_action("autosave_interval")->change_state(Glib::Variant<Glib::ustring>::create(v));
                restart_autosave_timer();
            },
            config.autosave_minutes ? std::to_string(config.autosave_minutes) : "off");

    m_win.add_action("choose_documents_dir", [this] {
        auto dialog = Gtk::FileDialog::create();
        dialog->set_title("Folder for your documents");
        dialog->set_initial_folder(Gio::File::create_for_path(path_to_string(get_documents_dir())));
        dialog->select_folder(m_win, [this, dialog](Glib::RefPtr<Gio::AsyncResult> &result) {
            try {
                auto folder = dialog->select_folder_finish(result);
                // backups made so far stay where they were; the next ones go to the new folder
                m_win.get_app().m_user_config.documents_dir = path_from_string(folder->get_path());
                std::error_code ec;
                std::filesystem::create_directories(get_documents_dir(), ec);
            }
            catch (const Glib::Error &) {
            }
        });
    });

    restart_autosave_timer();
    if (Glib::getenv("DUNE3D_AUTOMATION").empty())
        Glib::signal_timeout().connect_once([this] { offer_recovery(); }, 1500);
}

} // namespace dune3d

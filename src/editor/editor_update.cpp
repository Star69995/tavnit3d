#include "editor.hpp"
#include "dune3d_appwindow.hpp"
#include "dune3d_application.hpp"
#include "util/update_check.hpp"
#include "util/branding.hpp"
#include <glibmm/main.h>
#include <thread>

namespace dune3d {

/*
 * In-app updates: look for a newer release (GitHub "latest release" feed, see
 * util/update_check), offer it in a strip above the 3D view, download the
 * installer and run it silently; it closes this copy and starts the new one.
 * Network work happens on a thread, results come back via the main context.
 */

namespace {
// run fn on the main loop from any thread
void on_main(std::function<void()> fn)
{
    Glib::MainContext::get_default()->invoke([fn] {
        fn();
        return false;
    });
}

Gtk::Box &make_banner(const std::string &text, bool error = false)
{
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    box->add_css_class("update-banner");
    if (error)
        box->add_css_class("update-banner-error");
    auto la = Gtk::make_managed<Gtk::Label>(text);
    la->set_xalign(0);
    la->set_hexpand(true);
    la->set_wrap(true);
    box->append(*la);
    return *box;
}
} // namespace

void Editor::check_for_updates(bool interactive)
{
    const auto feed = get_update_feed_url();
    if (feed.empty()) {
        if (interactive)
            show_update_message("Updates aren't set up for this build.", false);
        return;
    }
    if (interactive)
        show_update_message("Checking for updates…", false);
    std::thread([this, feed, interactive] {
        try {
            auto info = check_for_update(feed, app_version);
            on_main([this, info, interactive] {
                if (info)
                    show_update_banner(*info);
                else if (interactive)
                    show_update_message(std::string("You have the latest version (") + app_version + ").", false);
            });
        }
        catch (const std::exception &e) {
            const std::string what = e.what();
            if (interactive)
                on_main([this, what] { show_update_message("Couldn't check for updates: " + what, true); });
        }
    }).detach();
}

void Editor::show_update_message(const std::string &text, bool error)
{
    auto &box = make_banner(text, error);
    auto close = Gtk::make_managed<Gtk::Button>();
    close->set_icon_name("window-close-symbolic");
    close->set_has_frame(false);
    close->signal_clicked().connect([this] { m_win.set_banner(nullptr); });
    box.append(*close);
    m_win.set_banner(&box);
}

void Editor::show_update_banner(const UpdateInfo &info)
{
    auto &box = make_banner(std::string(app_name) + " " + info.version + " is available. You have " + app_version
                            + ".");
    if (info.page_url.size()) {
        auto notes = Gtk::make_managed<Gtk::Button>("What's new");
        notes->set_has_frame(false);
        const auto url = info.page_url;
        notes->signal_clicked().connect([this, url] { Gtk::UriLauncher::create(url)->launch(m_win, [](auto &) {}); });
        box.append(*notes);
    }
    auto later = Gtk::make_managed<Gtk::Button>("Later");
    later->signal_clicked().connect([this] { m_win.set_banner(nullptr); });
    box.append(*later);
    auto update = Gtk::make_managed<Gtk::Button>("Update now");
    update->add_css_class("suggested-action");
    update->signal_clicked().connect([this, info] { start_update(info); });
    box.append(*update);
    m_win.set_banner(&box);
}

void Editor::start_update(const UpdateInfo &info)
{
    if (m_core.get_needs_save_any()) {
        show_update_message("Please save your documents first, then choose Update again.", true);
        Glib::signal_timeout().connect_once([this, info] { show_update_banner(info); }, 4000);
        return;
    }
    auto &box = make_banner("Downloading " + std::string(app_name) + " " + info.version + "…");
    auto progress = Gtk::make_managed<Gtk::ProgressBar>();
    progress->set_valign(Gtk::Align::CENTER);
    progress->set_size_request(200, -1);
    box.append(*progress);
    m_win.set_banner(&box);

    const auto dest = std::filesystem::temp_directory_path() / ("Tavnit3D-" + info.version + update_asset_suffix());
    std::thread([this, info, dest, progress] {
        try {
            download_file(info.installer_url, dest, [progress](uint64_t done, uint64_t total) {
                if (total)
                    on_main([progress, done, total] { progress->set_fraction((double)done / total); });
            });
            on_main([this, dest] {
                if (!launch_installer(dest)) {
                    show_update_message(launch_failure_message(), true);
                    return;
                }
                // the installer waits for us to exit, then starts the new version
                m_win.get_application()->quit();
            });
        }
        catch (const std::exception &e) {
            const std::string what = e.what();
            on_main([this, what] { show_update_message("Download failed: " + what, true); });
        }
    }).detach();
}

} // namespace dune3d

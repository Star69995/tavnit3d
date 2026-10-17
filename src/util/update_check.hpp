#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace dune3d {

struct UpdateInfo {
    std::string version;       // e.g. "0.2.0"
    std::string installer_url; // the release's asset for this system, see update_asset_suffix()
    std::string page_url;      // release page, for notes
    std::string notes;
};

// Release feed: a GitHub "latest release" API URL. DUNE3D_UPDATE_FEED overrides
// it (for testing); empty if updates aren't configured.
std::string get_update_feed_url();

// -1 / 0 / 1 for a < b / equal / a > b, comparing dotted numbers ("v" prefix ok)
int compare_versions(const std::string &a, const std::string &b);

// What the release asset for this system ends with: "-setup.exe" (Windows),
// "-x86_64.AppImage" / "-aarch64.AppImage" (Linux).
std::string update_asset_suffix();

// Parses a GitHub release JSON; returns it if it's newer than current and has an asset ending in suffix.
std::optional<UpdateInfo> parse_release(const std::string &json_text, const std::string &current,
                                        const std::string &suffix = update_asset_suffix());

// Blocking: fetches the feed and returns a newer release, if any. Throws on network errors.
std::optional<UpdateInfo> check_for_update(const std::string &feed_url, const std::string &current);

// Blocking download; progress gets (bytes so far, total or 0). Throws on errors.
void download_file(const std::string &url, const std::filesystem::path &dest,
                   const std::function<void(uint64_t, uint64_t)> &progress = {});

// Windows: starts the downloaded installer for a silent upgrade that restarts the
// app afterwards. Linux: replaces the running AppImage ($APPIMAGE) with the
// download and starts it once this process is gone. False if that isn't possible
// (e.g. not running from an AppImage). The caller quits right after a true.
bool launch_installer(const std::filesystem::path &installer);

// What to tell the user when launch_installer returned false.
std::string launch_failure_message();

} // namespace dune3d

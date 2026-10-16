// Version comparison and release parsing for the in-app updater.
#include "util/update_check.hpp"
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

static std::string release(const std::string &tag, bool with_installer = true, bool prerelease = false)
{
    std::string assets = with_installer ? R"([{"name": "Tavnit3D-x-setup.exe", "browser_download_url": "https://example.org/setup.exe"},
                                              {"name": "Tavnit3D-x-x86_64.AppImage", "browser_download_url": "https://example.org/app.AppImage"},
                                              {"name": "notes.txt", "browser_download_url": "https://example.org/notes.txt"}])"
                                        : R"([{"name": "source.zip", "browser_download_url": "https://example.org/s.zip"}])";
    return R"({"tag_name": ")" + tag + R"(", "html_url": "https://example.org/rel", "body": "fixes",
               "prerelease": )" + (prerelease ? "true" : "false") + R"(, "assets": )" + assets + "}";
}

int main()
{
    std::cout << "compare versions" << std::endl;
    check(compare_versions("0.1.0", "0.1.0") == 0, "equal");
    check(compare_versions("0.2.0", "0.1.9") == 1, "minor beats patch");
    check(compare_versions("0.10.0", "0.9.0") == 1, "numeric, not lexical");
    check(compare_versions("v1.0", "1.0.0") == 0, "v prefix and missing parts");
    check(compare_versions("1.0.0", "1.0.1") == -1, "older");

    std::cout << "parse release" << std::endl;
    auto newer = parse_release(release("v0.2.0"), "0.1.0", "-setup.exe");
    check(newer.has_value(), "newer release found");
    if (newer) {
        check(newer->version == "0.2.0", "version without v");
        check(newer->installer_url == "https://example.org/setup.exe", "picks the setup asset");
        check(newer->notes == "fixes" && newer->page_url == "https://example.org/rel", "notes and page");
    }
    check(!parse_release(release("v0.1.0"), "0.1.0", "-setup.exe"), "same version is no update");
    check(!parse_release(release("v0.0.9"), "0.1.0", "-setup.exe"), "older version is no update");
    check(!parse_release(release("v0.3.0", false), "0.1.0", "-setup.exe"), "no installer, no update");
    check(!parse_release(release("v0.3.0", true, true), "0.1.0", "-setup.exe"), "prereleases are skipped");
    {
        auto linux_update = parse_release(release("v0.2.0"), "0.1.0", "-x86_64.AppImage");
        check(linux_update.has_value(), "an AppImage asset counts on Linux");
        if (linux_update)
            check(linux_update->installer_url == "https://example.org/app.AppImage", "picks the AppImage asset");
    }
    check(!update_asset_suffix().empty(), "this system has an asset suffix");

    std::cout << (failures ? "FAILED" : "PASSED") << " (" << failures << " failures)" << std::endl;
    return failures ? 1 : 0;
}

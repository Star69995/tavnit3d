#include "new_window.hpp"
#include "util/fs_util.hpp"
#include <glibmm.h>
#ifdef _WIN32
#include <windows.h>
#endif

namespace dune3d {

static const char *secondary_env = "DUNE3D_SECONDARY_WINDOW";

bool is_secondary_window()
{
    return !Glib::getenv(secondary_env).empty();
}

static std::string get_executable()
{
#ifdef _WIN32
    wchar_t exe[MAX_PATH];
    const auto n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return {};
    return path_to_string(std::filesystem::path(std::wstring(exe, n)));
#else
    // an AppImage's own file, not the temporary mount it runs from
    const auto appimage = Glib::getenv("APPIMAGE");
    if (!appimage.empty())
        return appimage;
    std::error_code ec;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::string{} : path_to_string(self);
#endif
}

bool launch_new_window(const std::filesystem::path &open_file)
{
    const auto exe = get_executable();
    if (exe.empty())
        return false;
    std::vector<std::string> argv{exe};
    if (!open_file.empty())
        argv.push_back(path_to_string(open_file));
    auto env = Glib::listenv();
    std::vector<std::string> envp;
    for (const auto &name : env) {
        // a copy started for automation or tests shouldn't hand its script on
        if (name == secondary_env || name == "DUNE3D_AUTOMATION")
            continue;
        envp.push_back(name + "=" + Glib::getenv(name));
    }
    envp.push_back(std::string(secondary_env) + "=1");
    try {
        Glib::spawn_async("", argv, envp, Glib::SpawnFlags::DEFAULT);
    }
    catch (const Glib::Error &) {
        return false;
    }
    return true;
}

} // namespace dune3d

#include "dune3d_application.hpp"
#include "util/exception_util.hpp"
#include "util/branding.hpp"
#include "util/crash_report.hpp"
#include "util/util.hpp"
#include <string>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef HAVE_WAYLAND_PROXY
#include "wayland-proxy.h"
#endif

int main(int argc, char *argv[])
{

#ifdef HAVE_WAYLAND_PROXY
    // WaylandProxy::SetVerbose(false);

    // Create and run Wayland proxy in extra thread
    std::unique_ptr<WaylandProxy> wayland_proxy;
    if (getenv("WAYLAND_DISPLAY")) {
        wayland_proxy = WaylandProxy::Create();
        if (wayland_proxy) {
            wayland_proxy->RunThread();
        }
    }
#endif

#ifdef _WIN32
    // GTK's Windows input method (for composing East Asian text) crashed on a text field that
    // was gone; typing Hebrew, English ... works the same with GTK's own
    if (!getenv("GTK_IM_MODULE"))
        _putenv_s("GTK_IM_MODULE", "simple");
    // the crash report's message, from a process of its own: see util/crash_report.cpp
    if (argc == 2 && std::string(argv[1]) == "--crash-report")
        return dune3d::show_crash_message(dune3d::get_config_dir() / "crashes");
    // lets the installer wait until running copies are closed before updating files
    CreateMutexA(nullptr, FALSE, dune3d::app_mutex);
#endif

    auto application = dune3d::Dune3DApplication::create();
    dune3d::install_signal_exception_handler();
    // say what happened rather than just vanish
    dune3d::install_crash_report(dune3d::get_config_dir() / "crashes");

    // Start the application, showing the initial window,
    // and opening extra views for any files that it is asked to open,
    // for instance as a command-line parameter.
    // run() will return when the last window has been closed.
    auto rv = application->run(argc, argv);
#ifdef HAVE_WAYLAND_PROXY
    wayland_proxy = nullptr;
#endif
    return rv;
}

#pragma once
#include <filesystem>

namespace dune3d {

// When the app crashes (on Windows: a bad memory access, a C++ exception nobody caught, ...),
// writes what happened and where into a text file in dir and tells the user in a message
// box, with the way to report it, rather than just vanishing.
void install_crash_report(const std::filesystem::path &dir);

// The message about the newest report in dir. The crashed process starts a new one of itself
// with --crash-report for it: a message box from the crashed one could crash again (a crash
// inside a window's message handling does). Returns the exit code.
int show_crash_message(const std::filesystem::path &dir);

} // namespace dune3d

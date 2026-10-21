#pragma once
#include <filesystem>

namespace dune3d {

// Starts another copy of the program with its own window (and its own documents),
// optionally opening a file in it. False if it couldn't be started.
bool launch_new_window(const std::filesystem::path &open_file = {});

// True in a copy started by launch_new_window: it doesn't share the first copy's
// application instance and leaves the first one's backups alone.
bool is_secondary_window();

} // namespace dune3d

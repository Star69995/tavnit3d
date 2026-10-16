#pragma once

namespace dune3d {
// Name shown to users. This fork of Dune 3D carries its own name; credit to
// Dune 3D stays in the about dialog.
inline constexpr const char *app_name = "Tavnit 3D";
// our own release number (the Dune 3D base version is in version.py); keep in
// sync with tools/installer/tavnit3d.iss when releasing
inline constexpr const char *app_version = "0.1.0";
// GitHub "owner/repo" whose releases carry new versions; empty disables update checks
inline constexpr const char *update_repo = "Star69995/tavnit3d";
// named mutex the installer waits for before replacing files
inline constexpr const char *app_mutex = "Tavnit3DAppMutex";
} // namespace dune3d

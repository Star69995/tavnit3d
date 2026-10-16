# Project notes

Fork of [Dune 3D](https://github.com/dune3d/dune3d) (GPLv3, C++20, GTK4/gtkmm,
OpenCASCADE, SolveSpace solver). Goal: a friendlier, Shapr3D/Tinkercad-like
CAD on Windows, while sending generally useful improvements upstream as PRs.

## Branches and remotes

- `upstream` remote = dune3d/dune3d. `main` tracks upstream untouched.
- `studio` = our work. Keep commits focused so generic ones can be
  cherry-picked into upstream PR branches; UI restyling/branding stays here.

## Build, test, run (Windows)

Toolchain: MSYS2 at `C:\msys64`, UCRT64 environment, packages as in
`.github/workflows/win32.yml` (plus `mingw-w64-ucrt-x86_64-pybind11`).

- `tools\build.cmd` builds from any Windows shell (wraps `tools/build.sh`).
- `tools\build.cmd test` also runs `meson test` (tests live in `tests/`).
- `tools\build.cmd dist` makes the portable folder `dist/dune3d/` (+ zip);
  `dist/dune3d/dune3d.exe` runs without MSYS2.
- meson crashes with "Could not determine home directory" if `HOME` /
  `USERPROFILE` are unset; `tools/build.sh` sets them.

## Visual checks

- `tools/drive.ps1 -Steps "click:X:Y,key:...,drag:...,shot:PATH"` launches
  `build/dune3d.exe`, sizes the window to 1400x900, drives it with real mouse
  and key input and captures only the app window (PrintWindow), then closes
  it. Coordinates are window-relative, as seen in a previous shot.
- `tools/snap.ps1 -Out PATH [-Doc file.d3ddoc]` just launches and captures.
- Sample documents: https://github.com/dune3d/samples

## Code map (our additions)

- `src/document/primitives.*` - ready-made Box/Cylinder/Sphere, built from
  normal constrained sketch + extrude/revolve groups in a new body. GUI-free.
- `src/editor/editor_primitives.cpp` - editor glue for the above.
- `src/editor/group_editor/group_editor_extrude.cpp` - "Distance" field,
  backed by a distance constraint on the extrusion leader line.
- `src/dune3d.css` "Studio theme" section, `src/canvas/appearance.cpp` and
  `src/preferences/color_presets/Default.light.json` (keep the last two in
  sync) - look and feel.
- Tool palette: `Editor::create_action_bar_button` / `add_action_bar_section`.
- `meson.build`: core sources form the `dune3d_common` static library shared
  by the app and the tests; tests stub `Preferences::get()`.

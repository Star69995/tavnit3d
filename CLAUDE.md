# Project notes

**Tavnit 3D** - fork of [Dune 3D](https://github.com/dune3d/dune3d) (GPLv3,
C++20, GTK4/gtkmm, OpenCASCADE, SolveSpace solver). The user-facing name is
`app_name` in `src/util/branding.hpp` (plus window.ui/welcome texts and the
icon in `src/icons/`); the binary, file format and code namespace stay
`dune3d` to keep merging/PRs with upstream easy. Credit to Dune 3D stays in
the about dialog. Goal: a friendlier, Shapr3D/Tinkercad-like
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

- Preferred: `tools/ui.sh script.txt` - in-app UI automation
  (`src/editor/editor_automation.cpp`, enabled by `DUNE3D_AUTOMATION`):
  actions by catalog name, pointer on the 3D view, buttons/fields by label,
  `shot PATH` renders the window to PNG. Never touches the real mouse or
  keyboard; the window is parked off-screen. Popover menus aren't in shots.
- `tools/drive.ps1 -Steps "click:X:Y,key:...,drag:...,shot:PATH"` launches
  `build/dune3d.exe`, sizes the window to 1400x900, drives it with real mouse
  and key input and captures only the app window (PrintWindow), then closes
  it. Coordinates are window-relative, as seen in a previous shot. Shots
  composite the app's own popup windows (menus) too. It refuses to send input
  if it can't bring the app to the front (e.g. the user is typing elsewhere).
- `tools/snap.ps1 -Out PATH [-Doc file.d3ddoc]` just launches and captures.
- Sample documents: https://github.com/dune3d/samples

## Code map (our additions)

- `src/document/primitives.*` - ready-made Box/Cylinder/Sphere, built from
  normal constrained sketch + extrude/revolve groups in a new body, standing
  on the workplane at a position given by signed H/V distances from its
  origin (shown as X/Y in the object panel). `find_free_spot` places new
  ones next to existing solids. GUI-free. `body_solids.*`: final solid per body.
- Materials (Body::m_material: satin/matte/glossy/metal) and studio lighting
  live in `src/canvas/shaders/face-fragment.glsl`; per face group material via
  `ICanvas::add_face_group`, lighting toggle per workspace view (View menu).
- View bar under the axes cube (Fit/3D/Top/Front/Right); `Editor::zoom_to_fit`,
  `Canvas::zoom_to_fit`. New documents open in a 3D view, show only a
  200 mm XY plane (YZ/ZX hidden, see `Document::Document`).
- `src/editor/editor_primitives.cpp` - editor glue for the above.
- `src/editor/group_editor/group_editor_extrude.cpp` - "Distance" field,
  backed by a distance constraint on the extrusion leader line. Negative =
  against the workplane normal and switches the operation to Difference.
- `src/document/face_sketch.*` + `src/core/tools/tool_sketch_on_face.cpp` -
  "On face": ray-pick a flat face (OCC), new sketch with a workplane on it,
  rotation locked and origin pinned to an existing point so it follows edits.
- `src/dune3d.css` "Studio theme" section, `src/canvas/appearance.cpp` and
  `src/preferences/color_presets/Default.light.json` (keep the last two in
  sync) - look and feel.
- Object panel (`src/editor/editor_object.cpp`): picking a body in the
  workspace browser shows name/colour/visibility, every non-measurement
  dimension in the body's groups, "Edit features" and "Delete object"
  (`Editor::delete_body`, also in the body's context menu).
- Clicking a solid picks its body (`Editor::select_object_under_cursor`,
  `pick_body` in face_sketch.cpp, hidden bodies excluded via
  `Editor::body_is_shown`); dragging moves objects that have X/Y position
  dimensions (`src/editor/editor_object_drag.cpp`).
- Combine (object panel, `src/document/combine.*`): solid model operation
  group in a new body after everything; the sources get hidden, not removed.
- Tool palette: `Editor::create_action_bar_button` / `add_action_bar_section`.
- Grid on the active workplane (XY when none): `Renderer::visit(const EntityWorkplane &)`;
  spacing from `get_grid_step()` in `src/util/util.cpp`, user choice stored per
  workspace view (`WorkspaceView::m_grid_step`, View menu > Grid size), shown
  in the status bar next to the workplane label.
- `meson.build`: core sources form the `dune3d_common` static library shared
  by the app and the tests; tests stub `Preferences::get()`.

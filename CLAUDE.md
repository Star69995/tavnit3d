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
- `origin` remote = Star69995/tavnit3d (public; see "Public repo" below).
- `studio` = our work. Keep commits focused so generic ones can be
  cherry-picked into upstream PR branches; UI restyling/branding stays here.

## Build, test, run (Windows)

Toolchain: MSYS2 at `C:\msys64`, UCRT64 environment, packages as in
`.github/workflows/win32.yml` (plus `mingw-w64-ucrt-x86_64-pybind11`).

- `tools\build.cmd` builds from any Windows shell (wraps `tools/build.sh`).
- `tools\build.cmd test` also runs `meson test` (tests live in `tests/`).
- `tools\build.cmd dist` makes the portable folder `dist/dune3d/` (+ zip);
  `dist/dune3d/dune3d.exe` runs without MSYS2.
- `build/` is a debug build (-O0, assertions) for development. `dist` and
  `installer` build, test and package an optimised `build-release/`
  (`--buildtype=release`) instead, so what's released isn't the debug one.
- meson crashes with "Could not determine home directory" if `HOME` /
  `USERPROFILE` are unset; `tools/build.sh` sets them.

## Visual checks

- Preferred: `tools/ui.sh script.txt` - in-app UI automation (also commands for
  stickers, import, export-gltf, save, recipe, describe, find, handles,
  groups, render-window, zoom, close, panel-scroll, crash, check-solids, layout - see the top of editor_automation.cpp)
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
  A modal dialog (About, the Windows file dialog's GTK parent) blocks the main
  window: the `dialog` step sends the following input to it instead.
  Native Windows file dialogs need typing into their own window (Alt+N, path,
  Enter) from a second script that finds it by title.
- Site screenshots: lay the scene out with a recipe (`recipe PATH`) rather
  than clicks, then `action VIEW_FIT` and `zoom F X Y`.
- `tools/snap.ps1 -Out PATH [-Doc file.d3ddoc]` just launches and captures.
- Crashes and speed: `tests/ui/stress.txt` (many quick object panel changes
  on a joined table and a bowl, undo, `check-solids`) and `tests/test_performance.cpp`
  (drag step, part changes, rebuilds on bigger objects; fails well above today's
  times) run with the other tests. `tools/stress-panel.ps1` does it with the real
  mouse and keyboard (+/- clicks, typing, Alt+Shift, dragging; needs the computer
  to itself for a few minutes): it takes field positions from automation `layout`
  and reports a crash and its `crash-*.txt`. UI test scripts can use `@DIR@`.
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
  dimensions (`src/editor/editor_object_drag.cpp`). On a face of such an
  object a click picks the object even with a line within the 10 px hover
  radius (`Editor::ready_made_object_under_cursor`); only right on a line
  picks the line.
- Side panels: the mouse wheel over a number field scrolls the panel unless
  the field has focus (`wheel_scrolls_past_spinbuttons` in gtk_util, applied
  to the object panel and group editors). Keep the object panel's minimum
  width under the 300 px sidebar, or the whole sidebar widens.
- Duplicate (object panel / Ctrl+D, `src/document/duplicate.*`): copies a
  body's groups/entities/constraints with new UUIDs; constraints on
  generated entities are re-pointed by matching geometry after generating.
- Round edges (object panel, `src/document/round_edges.*`): a fillet group
  named "Rounded edges" at the end of the body over all non-seam edges. The
  view only shows groups up to the current one, so after adding it the panel
  moves the current group to the document's last group (like Duplicate/Combine).
- Sketch lines of bodies other than the current one are hidden by default
  (`Renderer::render`, View > Sketch lines of other objects).
- Open documents share one window; each new/opened one gets its own tab
  (workspace view) where only it is shown. The side list hides documents not
  shown in the current tab (`WorkspaceBrowser::update_documents`), unless
  View > Hidden documents in the list (`UserConfig::show_other_documents`). Document rows are bands (`.document-row`, the
  current one `.document-row-current`, dune3d.css) so one document's rows are
  told apart from the next.
- Combine (object panel, `src/document/combine.*`): solid model operation
  group in a new body after everything; the sources get hidden, not removed.
  Faces keep their source's colour and material through a join/cut (`calc_keep_colors` in
  `solid_model_solid_model_operation.cpp`: boolean history -> `SolidModelOcc::m_face_colors`/`m_face_materials`,
  `face::Face::material`). The renderer makes a face group per material, glTF a primitive per colour+material,
  STEP gets per-face colours (no materials in STEP). Fillet and hollow steps pass them on (`SolidModelOcc::inherit_looks`: face history; new faces along an edge take the look of the larger neighbour).
  It points at a source's last solid group: rounding/hollowing a source calls
  `repoint_combinations` (else removing the step would delete the join).
- Parts of joined objects (`src/document/object_parts.*`, panel in
  `src/editor/editor_object_parts.cpp`): the leaves of the combinations,
  grouped into alike sets (same shape/sizes/rounding, e.g. a table's legs)
  that change together unless split (`Editor::m_split_parts`, not saved).
  `PartsFollow`/`set_parts_dimension`: touching parts follow a size change
  (on top: goes up; under/beside: keeps to the nearest side). Positions:
  a raised part's own workplane origin (moving it needs generate pending, or
  the extrusion flips) or X/Y dims. Picking a part in the panel shows its
  handles (`m_object_editor_part`); a joined object drags as a whole.
- Eye switches and simple view (`EyeToggleButton` in `workspace_browser.cpp`, `UserConfig::simple_ui`,
  View > Simple toolbars): the list's show/hide switch is an eye (a cairo-drawn toggle, crossed out when
  hidden) with a tooltip per row kind (document / object / step) and a reason when greyed out; the cube
  next to an object's eye hides only the solid, the sketch lines stay. Rows have hover texts too (an
  object: select + right-click; a step: click goes back to it, later steps wait greyed). A hidden object
  (eye or cube off, `Editor::body_is_shown`) has no resize handles. Simple view: no document rows (the
  tree's root is the current document's objects, `WorkspaceBrowser::set_simple`), other documents not
  drawn, tab bar shown only while there are 2+ tabs (`Editor::update_workspace_tabs_visible`). The
  installer's toolbars page writes `%APPDATA%/dune3d/install_options.json` (`{"simple_ui": bool}`),
  which the app takes over once at start and deletes. Automation: `hide NAME`, `show NAME`,
  `handles-count N`; test `tests/ui/simple.txt`.
- Movable panels (`src/dune3d_appwindow_panels.cpp`): the tool palette and the layers list
  (workspace browser, `Dune3DAppWindow::set_layers_panel`) have a grip you drag. One capture
  `GestureDrag` on the window handles all of them (`m_panels`); dragging the layers header out of
  the sidebar floats it as an overlay on `canvas_overlay`, dropping it over the sidebar docks it
  again (also the button in its header). Places are kept in `UserConfig::panels` (margins in
  the overlay, clamped back in view on resize). Automation: `drag-panel NAME X Y`.
- Tool palette: `Editor::create_action_bar_button` / `add_action_bar_section`.
  A "Hide"/"Tools" button on top folds the whole palette away
  (`Dune3DAppWindow` constructor, remembered in `UserConfig::palette_folded`).
- Moving cues: the drag shows the offset in the key hint; Shift locks to one axis
  (`Canvas::is_shift_down`, updated on pointer motion); `Editor::get_move_hint` tells on hover how to move
  the picked object or a light. The floor (reference XY plane) is 500 mm by default and changes only by hand
  (View > Grid and floor settings, `Editor::set_floor_size`), never by itself. Opening an object (list row or
  click on it) leaves an earlier step behind (`Editor::go_to_last_group`) so it can be moved again. Lights
  slide on a horizontal plane at their height like objects (Shift: one direction); the dot below the lamp
  (`Light::get_height_handle`) and below its aimed-at point goes up/down (`ray_param_along_line`).
  Lifting (Z): a ready-made shape stands on the shared floor plane with X/Y dims, so it can't go up
  until `give_own_workplane` (`duplicate.cpp`) makes a workplane of its own in its first group and
  re-points the sketch, dimensions and steps at it (JSON `remap`, as Duplicate does); after that
  `get_part_position`/`set_part_position` move its origin in 3D. The new plane has
  `EntityWorkplane::m_origin_fixed` (the solver keeps its origin; otherwise it meets the body's
  helper planes halfway and revolves/outlines end up half lifted), and `set_part_position` moves
  those helper planes along first. UI: "Z" row (underside height; `Editor::add_lift_row`) and the
  "Lift" handle (`Kind::LIFT` in `editor_handles.cpp`, dragged along the world Z line); both
  convert lazily on the first real change. Tests: `test_lifting` in `tests/test_primitives.cpp`,
  `tests/ui/lift.txt` (`drag-handle LABEL DX DY`, `expect-bottom MM`).
- Smooth dragging: `Document::update_pending(..., interactive=true)` (used by
  `Core::solve_current`, object and handle drags) skips solid models whose last
  build (`Group::m_solid_model_ms`) would blow a 40 ms budget, and everything
  after; waiting ones after a fresh one in the same body get
  `m_solid_model_deferred` and the renderer skips them, so the object shows its
  live outer shape. `Editor::schedule_deferred_solid_models` builds them when
  the pointer rests 150 ms; a commit/rebuild builds them anyway.
- Grid on the active workplane (XY when none): `Renderer::visit(const EntityWorkplane &)`;
  spacing from `get_grid_step()` in `src/util/util.cpp`, user choice stored per
  workspace view (`WorkspaceView::m_grid_step`, View menu > Grid size), shown
  in the status bar next to the workplane label.
- `meson.build`: core sources form the `dune3d_common` static library shared
  by the app and the tests; tests stub `Preferences::get()`.
- Materials: `src/document/material.*` is the one table (PBR values, labels,
  `material_from_pbr` for imports); glass is drawn in a second, blended pass
  (`FaceRenderer::render(bool glass)`). Shading for faces and stickers lives in
  `src/canvas/shaders/lighting.glsl` (included as `##lighting`). Wood (material 5) is procedural in
  that shader (`wood_color`: grain along X in world mm, body colour stains it; glTF gets a flat wood colour).
- Stickers (`src/document/decal.*`, `Body::m_decals`): a picture projected along
  a normal onto the body's triangles (`make_decal_mesh`), anchored to the
  body's bounds centre so it follows moves; pictures saved like picture
  entities (`.d3dpic` folder). Drawn by `src/canvas/decal_renderer.*`; placed
  by clicking (`src/editor/editor_decal.cpp`, `pick_surface_point`).
- Light symbols: `draw_light_symbol` in `src/render/renderer.cpp` draws a globe (+ rays for a point lamp, a beam
  cone for a spot, rays and parallel arrows for the sun) in 3D lines, sized by the light-to-target distance.
- Lights (`src/document/light.*`, `Document::m_lights`): brightness is as seen
  at the target; `src/editor/editor_lights.cpp` (palette, panel, picking the
  marker by screen distance, which also starts dragging the lamp or, for spot/sun, its aimed-at
  diamond on a camera-facing plane: `update_light_drag`, one undo step on release); the canvas gets
  them via `Canvas::set_lights`. Test: `tests/ui/lights.txt` finds lights by name (`drag-light`, `light-mark`, `expect-light*`, `expect-lights`), not by pixel, so layout changes don't make it miss.
- glTF export (`src/document/export_gltf.*`): own GLB writer, metres/y-up,
  one primitive per face colour, stickers as textured primitives,
  KHR_lights_punctual, the view as camera.
- Import (`src/document/import_objects.*` + `src/import_step/step_importer.cpp`):
  STEP/IGES/glTF/OBJ through OCCT's XCAF readers, grouped into one object per
  appearance (`ImportPart`), referenced by an `EntitySTEP` with `m_part`;
  per-face colours survive via `SolidModelOcc::m_face_colors`. Meshes are
  surface-less faces: the triangulator keeps them as they are (`add_mesh`).
- Render (`src/util/blender_render.*`, `src/editor/render_window.*`): runs
  Blender's Cycles on the glTF in the background (script embedded), the app
  puts the transparent result on the chosen background. `DUNE3D_BLENDER`
  points at a blender.exe for tests.
- Hollow (`GroupShell`, `src/document/hollow.*`, `solid_model_shell.cpp`): a
  thick-solid group before the "Rounded edges" fillet, which now finds its
  edges again on every rebuild (`GroupLocalOperation::m_all_edges`).
- Positions: every ready-made shape has X/Y dimensions from the workplane
  origin to a point its sketch hangs on. Revolved profiles stand on an upright
  plane, extruded outlines lie on a flat plane of their own (`add_flat_plane`
  in primitives.cpp), so the X/Y moves them whole. A full 360 degree revolve
  keeps its angle pinned in the solver (`System::add(const GroupRevolve &)`);
  without that, moving a smooth profile shrank the turn. Joined objects
  are moved through their parts (see Parts above).
- Links (`website_url`, `source_url`, `issues_url` in branding.hpp): about
  dialog credits and main menu (Website, Source code on GitHub, Report a problem).
- Recipes (`src/document/recipe.*`): JSON descriptions of objects built from
  `add_primitive` (sizes, `Placement`), `add_revolved_profile`,
  `add_extruded_outline`, hollow, rounding, combining. Bowl/Vase are recipes;
  `src/document/recipe_ai.*` asks an AI service for one: `ai_providers()` (Claude
  via its own API with structured outputs, plus OpenAI-style chat completions for
  OpenAI/Gemini/OpenRouter/"Other" with a custom address), `AiSettings`
  (`UserConfig::ai`: provider, key/model/url per provider id, so a key never goes to
  another service; old `claude_api_key` is migrated). Without a key,
  `make_chatbot_prompt` (instructions + schema + description) is copied from
  `DescribeWindow` and `parse_recipe_text` reads the pasted answer (fences and chatter
  tolerated). `DUNE3D_CLAUDE_URL` overrides the Claude endpoint for tests; tests in
  `tests/test_recipe_ai.cpp`. HTTP: `src/util/http.*` (WinHTTP/libcurl).
- Resize handles (`src/editor/editor_handles.cpp`) use the object panel's
  dimension list (`src/document/object_dimensions.*`; labels like Width/Depth/
  Height come from the constraint type and the plane it's on).
- Autosave (`src/editor/editor_autosave.cpp`, `Core::write_backup`): every
  `UserConfig::autosave_minutes` (menu: Back up unsaved work, 0 = off) each
  document with unsaved changes is copied to `<documents dir>/Autosave/<uuid>.d3ddoc`
  (+ `.json` with name/original path); removed when the document is saved or
  closed, offered back at the next start if still there (copied to the
  documents folder as "Recovered ..."). The documents folder
  (`UserConfig::documents_dir`, default Documents/Tavnit 3D, created at start)
  is also where open/save dialogs start.
- More shapes: hexagon, triangle prism, star, heart, half sphere are recipes
  (`src/editor/editor_recipe.cpp`, like Bowl/Vase). No pyramid yet (needs a
  tapered extrude or loft). Threads: see Screw.
- Holes (`Body::m_hole`, object panel "Hole" switch + "Cut"): drawn as glass,
  left out of STL/glTF export; `Editor::cut_with_hole` subtracts it from every
  shown non-hole object whose bounds it touches (`add_combination`).
- Picture object (`Editor::add_picture_object` in editor_decal.cpp): a 1 mm
  white box with the picture as a sticker on its big face; resizable like a box.
- Screw (`src/document/screw.*`, `Editor::add_screw`, `src/editor/screw_window.*`): palette
  button opens a window (head: hexagon/socket/pan/countersunk, M3-M12, length, thread on/off);
  `make_screw_recipe` builds a cylinder shank at the thread root + head parts joined, recess
  subtracted. Real helical thread: `GroupThread` (`Group::Type::THREAD`,
  `solid_model_thread.cpp`, `thread.*`, recipe key `"thread"`): a ridge swept along a helix
  (BRepOffsetAPI_MakePipeShell, binormal mode) fused to the upright cylinder, tip cut to a cone.
  Gotchas: fusing a ridge that was first trimmed with Common silently returns just the core, so
  trim the fused result instead; `BRepBndLib::Add` is loose on curved faces (use `AddOptimal`).
  Only upright (not rotated) cylinders; thread max 32 turns from the tip, rest plain. Automation:
  `screw HEAD D L [plain]`; tests `tests/test_screw.cpp`, `tests/ui/screw.txt`. The AI prompt
  (`recipe_ai.cpp`) and recipe schema know `thread`.
- Copies in a row (object panel, `Editor::copy_in_a_row`): N x `duplicate_body`
  with an X offset of width + gap; only for objects that have an X position.
- Round hole (palette "Hole"): a 6 x 30 mm cylinder created as a hole body
  (`Editor::add_primitive(..., as_hole)`); move it into an object and press Cut.
- Tool palette Shapes: the common shapes are always shown; Tube, Torus,
  Hexagon, Triangle, Star, Heart, Dome, Screw, Hole, Bowl, Vase fold away
  behind "More" (`Dune3DAppWindow::add_action_bar_collapsible_section`).
- Patterns (`src/document/pattern.*`, `Decal::m_repeat`): stripes/dots/checker/grid
  as a generated transparent tile used as a sticker that repeats (`m_width` =
  tile size in mm). `make_decal_mesh` then covers every face looking within
  60 degrees of the clicked normal, uv unclipped; the decal renderer sets
  GL_REPEAT per decal, glTF export uses a repeating sampler. Panel: Stickers >
  Pattern + colour + "Add pattern..." (click on the object); automation:
  `pattern N [C]`. Tests: `tests/test_patterns.cpp`, `tests/ui/patterns.txt`.
- Object panel refresh: `Editor::schedule_object_panel_refresh` (after a
  rebuild, or a panel change that set `m_object_editor_keep`) makes the panel
  again only once no mouse button is held. Never rebuild the panel straight
  from a field's handler: GTK crashed when a +/- still getting clicks was gone.
- Hover hints (`src/editor/panel_hints.*`): `get_action_hint` adds a sentence to each palette
  button's tooltip; `add_panel_hints` gives object/light panel rows a hint by their label (a
  row's own tooltip wins). New palette buttons or panel rows: add their hint there.
  The side panel keeps its scroll position across panel rebuilds (`restore_side_panel_scroll`).
- Crash report (`src/util/crash_report.*`, Windows): unhandled exceptions and
  uncaught C++ exceptions write `%LOCALAPPDATA%/dune3d/crashes/crash-*.txt`
  (module+offset per call; resolve with `addr2line -e build-release/dune3d.exe`
  or the debug build) and show a message box. Automation `crash` tries it.
- Find (`src/editor/action_search.*`, items in `editor_search.cpp`): Ctrl+K
  both as the FIND action and a window-level capture shortcut. When a
  feature lands, add it there with where it lives.

## Gotchas

- nlohmann enum conversions defined with NLOHMANN_JSON_SERIALIZE_ENUM in one
  .cpp must not be instantiated elsewhere (`j.get<EnumType>()` in another
  file silently instantiates a numeric version and the linker may pick it
  for everyone - this broke loading documents once). Use helpers like
  `Entity::deserialize_type`.
- Changing a document directly (not through a tool): mark groups pending
  *before* deleting anything they reference, then `m_core.rebuild(...)`.

## Public repo, website, releases

- GitHub: `origin` = https://github.com/Star69995/tavnit3d (public, a GitHub
  fork of dune3d/dune3d), default branch `studio`. Upstream's own CI
  workflows (all/freebsd/macos/win32.yml) are disabled there via
  `gh workflow disable`; only `pages.yml` runs.
- `site/` - the public landing/download page, https://star69995.github.io/tavnit3d/
  (GitHub Pages, deployed by `.github/workflows/pages.yml` on pushes to
  `studio` touching `site/`). Static HTML/CSS/JS, no build step, two
  languages: `site/index.html` (Hebrew, RTL, the root) and `site/en/index.html`
  (English, LTR), linked by the switch in the top bar and `hreflang` tags;
  they share `style.css`, `app.js` (strings in its `STRINGS` table, picked by
  `<html lang>`) and `img/`. Any content change goes into both pages, and
  the icon sprite is identical in both. Release notes are shown on both, so
  write them Hebrew first, then English (`app.js` sets each block's direction).
  structure, `style.css` and `app.js` come from the sibling `backuper` project.
  Preview: `python -m http.server 8766` in `site/`. `app.js` fills the
  download buttons and version list from the GitHub Releases API at runtime
  (installer = the `*-setup.exe` asset). When a user-visible feature lands or
  changes, update the feature cards / tour / FAQ in the same pass; don't
  claim what the app doesn't do or announce a feature before its release.
  Screenshots in `site/img/*.webp` are real app shots (`tools/ui.sh` scripts
  with `shot`, 1400x900, converted with `cwebp -q 82`); `og.png` is a
  1200x630 shot of the deployed Hebrew page's top, `og-en.png` the same for /en/.
- Credits: the README, the site's "מאיפה הקוד" section and the about dialog
  (`src/widgets/about_dialog.cpp`) say it's a Dune 3D fork and that the
  changes were written mostly by Claude, directed by Star. Keep that true.
  Avoid gendered Hebrew verbs/adjectives for Star on the site.

### Linux (AppImage + in-app updates)

- `tools/appimage.sh` builds `dist/Tavnit3D-<ver>-x86_64.AppImage` with
  linuxdeploy (+ GTK plugin); needs the packages of `.github/workflows/all.yml`,
  `libcurl4-openssl-dev`, `patchelf`. Built on Ubuntu 24.04, so it needs glibc >= 2.39
  on the user's system. `.github/workflows/linux-appimage.yml` runs it when a
  release is published (by `tools/release.sh` from Windows) and attaches the file.
- HTTPS outside Windows: libcurl (`src/util/http.cpp`, `HAVE_CURL` from meson).
  This also makes "make from a description" work on Linux.
- Updates: `update_asset_suffix()` picks the release asset per system
  (`-setup.exe`, `-x86_64.AppImage`). On Linux `launch_installer` replaces the
  running file (`$APPIMAGE`) with the download and starts it 2 s after we exit;
  from a plain build (no `$APPIMAGE`) it returns false and nothing is replaced.
- Linux checks without Windows: `meson setup build && ninja -C build`, run the UI
  tests with `xvfb-run meson test -C build` (needs python3 with gi: use the
  system python3.12 on Ubuntu 24.04 if another python is first in PATH).

### Releasing (installer + in-app updates)

1. Bump `app_version` in `src/util/branding.hpp` (installer reads it), commit, push.
2. `tools/release.sh [notes.md] [--yes]` - checks the tag is new on GitHub
   and HEAD is pushed, runs `tools/build.sh test dist installer`, shows the
   notes (default: commit subjects since the last release) and asks, then
   publishes release `v<ver>` with `Tavnit3D-<ver>-setup.exe` and
   `Tavnit3D-<ver>-portable.zip` (Inno Setup 6: `winget install JRSoftware.InnoSetup`).
   The site and the in-app updater pick it up by themselves; drafts and
   prereleases are ignored.
3. Running copies check `releases/latest` of `update_repo` at start (toggle in
   the main menu), show a strip above the 3D view with "Update now", download
   the installer, run it with `/SILENT /RELAUNCH=1` and quit; the installer
   waits on the app's mutex and restarts the new version. Code:
   `src/util/update_check.*`, `src/editor/editor_update.cpp`.
   `DUNE3D_UPDATE_FEED=<url>` overrides the feed for testing (a local
   `python -m http.server` with a release.json works).
- Gotcha: the local clone has upstream's tags (v0.9.0 ... v1.4.0). Our tags
  live only on GitHub (`gh release create` makes them); never `git push --tags`.
  Our own 0.9.x would clash with upstream's names locally - skip from 0.8.x to 0.10.0.

## Web UI rules (for `site/`)

Inlined from ~/.claude/web-ui-rules.md; keep in sync with it.

Apply only in projects with a browser-based user interface.

- Use `-` (hyphen), never `—` (em dash), in UI copy/text.
- Minimum text size: 12px. Layout must stay usable down to a 320px viewport width.
- Focusable fields (`input`, `textarea`, `select`) need `font-size: 16px` or larger - this overrides the 12px minimum above. Below 16px, iOS Safari zooms the page in when the field gets focus, and that zoom shifts the visual viewport, so sticky/fixed bars drift off screen and the user has to pinch back out. Never solve it with `user-scalable=no` or a permanent `maximum-scale=1` in the viewport meta: Safari ignores the former anyway, and both block pinch-zoom for everyone for the whole visit. If a field genuinely must show smaller text, add `maximum-scale=1` to the viewport meta on `focusin` and remove it on `focusout`.
- **iOS Safari's on-screen keyboard shrinks and offsets the *visual* viewport without touching the *layout* viewport, and doesn't fire `visualViewport` events continuously during its own open/close animation** - any sticky/fixed bar (header, toolbar, action bar) can drift off-screen or end up hidden behind the keyboard. This does not reproduce in a desktop browser or in the iOS Simulator's software keyboard - it needs a real device to test. Extensively tested across many real-device approaches, on a page with a *stack* of several sticky bars (not just one): the fix that looks obvious - give each bar its own `transform`/`top` that chases `window.visualViewport.offsetTop` - is fragile once there's more than one bar. It breaks in different ways depending on the exact implementation: z-index ordering between the bars becomes load-bearing and easy to get backwards (a bar with no explicit `z-index` silently paints *underneath* a sibling that has one, even if it's later in the DOM), and WebKit can glitch-repaint (a bar flickers transparent/disappears) even with a permanently-applied `transform` meant to keep it on a stable GPU layer. The approach that held up cleanly, repeatedly: don't move each bar - make the **app's own outer shell** the thing that tracks the keyboard. Give the shell `position: fixed` with its `top` and `height` bound to CSS variables kept in sync with `visualViewport.offsetTop` and `visualViewport.height` (a `resize`/`scroll` listener on `window.visualViewport`, refreshed every frame via `requestAnimationFrame` for as long as a field has focus - listeners alone leave the frame visibly lagging behind the keyboard's open/close animation). With the shell's own geometry always matching what's actually visible, everything inside it - header, sticky toolbars - can use plain normal-flow/sticky positioning with no per-element `transform` or `top` hacks at all: there's nothing to chase, so there's no overlap or stacking-order question to get wrong. Separately, and just as easy to miss: scroll the newly-focused field into view **on focus**, not only once the user starts typing - a field near the bottom of scrollable content can end up hidden under the keyboard the instant it opens, before any keystroke, and no browser does this for you automatically inside a custom scroll container.
- **A persistent bottom bar (bottom nav / tab bar) is hidden while a text field has focus on touch devices, and shown again only when the scroll container is at its very end.** With the on-screen keyboard open, a bar parked between the content and the keyboard just eats scarce visible height; at the end of the content there's nothing left below it to hide, so it comes back. Gate it on `(pointer: coarse)` (on desktop there's no keyboard, so hiding it looks like a glitch). Use *different* reveal/hide distance-from-bottom thresholds (e.g. reveal within 4px, hide beyond 96px) rather than a single toggle point: the bar's own show/hide changes the scroll container's `clientHeight` by its height, which would otherwise flip the "at bottom" check back and forth. Re-check after the keyboard's open animation settles (~350ms after `focusin`), and animate with `max-height` + `opacity`, not `display: none`. Reference: `useHideWhileTyping` in `src/components/BottomNav.tsx`.
- **Don't layer a manual "scroll the caret/focused element into view" correction on top of an auto-scroll mechanism that already exists** (a rich-text editor's own built-in scrollIntoView, or the browser's native reveal-on-focus scrolling) - **two scrollers acting on the same container in the same animation frame is what produces a visible jump/overcorrection, not a missing feature.** This surfaced as: typing in the *last* line of a scrollable editor on iOS Safari made the whole view jump to the top instead of scrolling down slightly to reveal the caret - happened only there, not mid-document, because with no more room to scroll down, the browser's own reveal-scroll escalates the gesture to an ancestor/the whole page instead of the exhausted inner container, and a second corrective `scrollBy` fighting it in the same frame is what turned that into a jump rather than a no-op. Diagnosing this kind of "two scrollers fighting" bug: put a kill switch on each of your own correction mechanisms and disable them all first - if the symptom disappears completely, your code (not the platform) is the thing to fix, and you can binary-search which mechanism and which direction (scrolling up to reveal a caret hidden under sticky top bars, vs. scrolling down to reveal one hidden under the keyboard) is the culprit by re-enabling one branch at a time. The fix that held up: don't add a second scroll call after the fact - find the hook that lets you feed the *existing* mechanism what it doesn't already know (e.g. ProseMirror's `editorProps.scrollThreshold`/`scrollMargin`, which it already applies on every content transaction) so there's still only one scroller. Concretely, for an editor with sticky top bars covering part of the scroll container: set `scrollThreshold.top`/`scrollMargin.top` to the sticky stack's covered height so the editor's own scrollIntoView already treats that height as hidden, and **leave the bottom/keyboard side at its default** - the editor and the browser already handle "caret hidden under the keyboard" correctly on their own, and adding a bottom-side correction is what reintroduces the fight. If the covered height can change (a bar that's conditionally shown, a toolbar that wraps to two lines at narrow widths), make the threshold/margin a live getter, not a value computed once - re-measure the DOM on each read rather than caching a stale number.
- Lean toward centering elements rather than just dumping them against the start edge (the right side in RTL). Empty states, page headers/intros, standalone cards, forms, modals' content, call-to-action buttons and narrow content blocks should be visually centered (in a constrained max-width column where appropriate) unless there's a clear reason for start alignment - e.g. long running text, table/list rows, or form labels inside a field group stay start-aligned for readability.
- No emoji in UI elements - use an icon library or SVG instead.
- Never use native browser dialogs (`alert()`, `confirm()`, `prompt()`) for user-facing interaction - always build a custom in-app modal component instead. Native dialogs can't be styled to match the app, don't respect the page's language/RTL direction or button-label conventions, and (`confirm`/`prompt`) block the JS thread while open.
- Every site needs Open Graph + Twitter Card meta tags in `<head>` (`og:title`, `og:description`, `og:image`, `og:url`, `og:image:width`/`height`, `og:locale` set to the site's actual content language (e.g. `he_IL`, not the visitor's), `og:image:alt` describing what's shown in the image, `twitter:card` set to `summary_large_image`), pointing at an absolute image URL. The preview image (1200x630) is a designed brand card, not a screenshot: the project's logo (from its original source file), the site name, a short tagline, on a clean background in the brand colors, readable at small sizes (e.g. a Slack/WhatsApp link preview). Generate it with a script kept in the repo (e.g. `scripts/generate-og-image.js`) so it can be regenerated from the logo instead of being edited by hand, and mention the script in the README. If the project has no logo yet, ask for one rather than inventing artwork. Keep the card's text in sync with `og:title`/`og:description`.
- **Text editors with mixed Hebrew/English (or any RTL + LTR) multi-line content: give every line its own explicit direction, and an empty line inherits the direction of the line above it.** A single `dir` on the field cannot get both languages right: `dir=rtl` flips English lines (right-aligned, trailing period jumps to the start), and `dir=auto`/`unicode-bidi: plaintext` leave a new empty line with no strong character, so after a Hebrew paragraph the caret lands on the left. An invisible RLM/LRM inserted on Return fixes the caret but then wins over what is typed next (English typed after it stays right-aligned) and pollutes the saved text - don't use it. What held up (verified in Chrome and on a real iPhone; the extra hardening below is what made it hold on iOS): build the editor as a `contenteditable` with one block (`div`) per line; on `input`, `beforeinput` (re-check in a microtask), `compositionstart/update/end`, `keyup`, `focus` and `selectionchange`, set each block's direction from its first strong character, falling back to the previous block's direction for an empty block (and to the page default for the first). Apply it as `data-dir="rtl|ltr"` plus the `dir` attribute, and enforce it in CSS with `!important` (`direction`, `text-align`, `unicode-bidi: isolate`) because **iOS Safari writes its own direction onto the block (a `dir` attribute or inline `direction`/`unicode-bidi`, sometimes on a wrapper span) when the keyboard language is switched**, which otherwise beats yours and leaves the line stuck on the old direction even after typing in the other language. On every sync, strip foreign `direction`/`unicode-bidi`/`text-align` inline styles and nested `dir` attributes, and wrap loose text nodes in their own block. Reference implementation: the `blockdir` behavior in `lab.js` of the `hebrew-caret-lab` branch of the `ios-lab` repo.
- **The same per-line direction rule in a Tiptap/ProseMirror editor: a `dir` node attribute on every paragraph/heading, recomputed in `appendTransaction`** (verified on a real iPhone with Tiptap 2.11.5 StarterKit; `dir=auto` on the editor root has the same problem as on a plain field). Add a global attribute `dir` (`default: null`, `parseHTML: el => el.getAttribute('dir')`, `renderHTML: a => a.dir ? { dir: a.dir, 'data-dir': a.dir } : {}`) on `paragraph` and `heading`, and a ProseMirror `Plugin` whose `appendTransaction` returns `null` unless a transaction has `docChanged`, then computes each top-level block's direction (first strong character of its text, else the previous block's, else the page default) and calls `tr.setNodeMarkup(pos, undefined, { ...node.attrs, dir })` only for blocks whose `dir` differs. **Only touch node types that declare the attribute (`paragraph`, `heading`)**, or `appendTransaction` never settles and loops forever. Seed the initial document with the `dir` values already computed (a plugin that only reacts to `docChanged` does not run on load). Enforce it with the same `!important` CSS on `[data-dir]` as for plain `contenteditable` (`direction`, `text-align`, `unicode-bidi: isolate`), so a direction iOS writes inline when the keyboard language switches cannot win; don't fight ProseMirror by stripping inline styles itself. Set `.ProseMirror p { margin: 0 }` if lines should behave like lines. Import `Plugin` from `@tiptap/pm/state` at the same version as the rest of Tiptap. Reference implementation: `blockDirExtension` in `lab.js` of the `hebrew-caret-lab` branch of the `ios-lab` repo (setup `tt-blockdir`).

## General rules (copied from ~/.claude/CLAUDE.md)

Inlined here so they also apply when working from another machine. Keep in
sync with the global file when it changes.

### Testing is mandatory (also in cloud sessions)

Every change must be tested before it is reported done or pushed: run the
repo's own tests/checks (`meson test`, UI scripts via `tools/ui.sh`, etc.) and,
for anything visible, check it in the running app. If a test can't run, say so
and why - never claim "done" without a test result. You may install any tool
needed for testing and development (packages via apt/pip/npm, xvfb, a browser,
linters, build deps) on your own initiative, including in cloud/remote
sessions. See "Linux checks without Windows" above for the cloud setup.

### Dev/test servers

If a task would benefit from a running dev/test server (`npm run dev`,
`wrangler dev`, etc.) — e.g. visual verification of a UI change — you may
start one yourself on your own initiative, in the background. Always close
it yourself when you're done with it (don't leave it running after your
task finishes). Don't stop a server you didn't start yourself without
asking first — it may be the user's own long-running terminal session. If
you notice orphaned/duplicate server processes you didn't start, point them
out and ask before killing them.

Exception: if the user explicitly asks you to start/stop/restart a server,
or a project's own CLAUDE.md explicitly says otherwise, follow that instead.

### Code quality

Favor reuse over duplication: before writing new code, check for an
existing function/component/util that already does this and extend or
call it instead of copying logic.

### Docs upkeep

After a structural change (new module, moved files, changed architecture
or conventions), update the project's CLAUDE.md (or create one if it
doesn't exist yet) and its README.md in the same pass, so neither drifts
from the code.

### New web UI projects

When a project has (or starts getting) a browser-based UI and its
CLAUDE.md doesn't yet contain the web UI rules, add them yourself
(create the CLAUDE.md if it doesn't exist), without waiting to be asked.

Always inline the actual rule text from `~/.claude/web-ui-rules.md`
into the project's own CLAUDE.md - never a bare `@~/.claude/web-ui-rules.md`
import. The user wants the rules visible/portable on any machine or to
any collaborator, even without this home directory.

### OG image upkeep

After making a significant visual/design change to a project's UI (layout,
color scheme, hero/main content, major section additions or removals),
ask the user whether the Open Graph preview image (`og:image`) should be
regenerated to match, rather than silently leaving a stale image in
place. A small copy tweak or unrelated bug fix doesn't warrant asking.

### Visual/browser testing tools

If a task needs real visual verification (checking a rendered layout,
responsive breakpoints, screenshots) and no browser/screenshot tool is
currently available, you may install and register one yourself on your own
initiative — e.g. adding `chrome-devtools-mcp` via
`claude mcp add -s user chrome-devtools -- npx -y chrome-devtools-mcp@latest`.
Prefer user scope (`-s user`) so it's available across all projects, unless
the task calls for a project-specific tool. Note that a newly added MCP
server's tools only become available starting the next session — say so
rather than assuming it's usable immediately in the current one.

#pragma once
#include "editor_interface.hpp"
#include "core/core.hpp"
#include <gtkmm.h>
#include "document/primitives.hpp"
#include "document/light.hpp"
#include "document/group/group_shell.hpp"
#include "document/object_parts.hpp"
#include "document/group/igroup_solid_model.hpp"
#include "action/action.hpp"
#include "preferences/preferences.hpp"
#include "workspace/workspace_view.hpp"
#include "dialogs/dialogs.hpp"
#include "util/action_label.hpp"
#include "document/group/group.hpp"
#include "selection_menu_creator.hpp"
#include "idocument_view_provider.hpp"

namespace dune3d {

// where along a line the pointer ray comes closest, in mm from its start point (editor_handles.cpp)
double ray_param_along_line(const glm::dvec3 &start, const glm::dvec3 &dir,
                            const std::pair<glm::dvec3, glm::dvec3> &ray);

class Dune3DApplication;
class Preferences;
enum class ToolID;
class ActionLabelInfo;
class ToolPopover;
class ConstraintsBox;
class GroupEditor;
class SelectionEditor;
class WorkspaceBrowser;
class Dune3DAppWindow;
class Canvas;
class ClippingPlaneWindow;
class SelectionFilterWindow;
class Buffer;
enum class SelectionMode;
enum class CommitMode;
enum class WorkspaceBrowserAddGroupMode;

class Editor : private EditorInterface, private IDocumentViewProvider {
public:
    Editor(Dune3DAppWindow &win, Preferences &prefs);

    void init();


    void tool_bar_set_actions(const std::vector<ActionLabelInfo> &labels) override;
    void tool_bar_set_tool_tip(const std::string &s) override;
    void tool_bar_flash(const std::string &s) override;
    void tool_bar_flash_replace(const std::string &s) override;
    glm::dvec3 get_cursor_pos() const override;
    glm::vec3 get_cam_normal() const override;
    glm::quat get_cam_quat() const override;
    glm::dvec3 get_cursor_pos_for_plane(glm::dvec3 origin, glm::dvec3 normal) const override;
    std::pair<glm::dvec3, glm::dvec3> get_cursor_ray() const override;
    double get_grid_snap(const EntityWorkplane &wrkpl) const override;
    bool body_is_shown(const UUID &body) const override;
    void tool_update_data(std::unique_ptr<ToolData> data) override;
    void enable_hover_selection(bool enable) override;
    std::optional<SelectableRef> get_hover_selection() const override;
    void set_no_canvas_update(bool v) override
    {
        m_no_canvas_update = v;
    }
    void canvas_update_from_tool() override;

    void set_solid_model_edge_select_mode(bool v) override
    {
        m_solid_model_edge_select_mode = v;
    }

    bool get_use_workplane() const override;

    void set_canvas_selection_mode(SelectionMode mode) override;

    void show_delete_items_popup(const ItemsToDelete &items_selected, const ItemsToDelete &items_all) override;

    Glib::RefPtr<Pango::Context> get_pango_context() override;

    void set_buffer(std::unique_ptr<const Buffer> buffer) override;
    const Buffer *get_buffer() const override;
    void set_first_update_group(const UUID &group) override;


    void open_file(const std::filesystem::path &path);
    bool has_file(const std::filesystem::path &path);

    ~Editor();

private:
    void init_workspace_browser();
    void init_properties_notebook();
    void init_header_bar();
    void init_actions();
    void init_tool_popover();
    void init_canvas();
    void init_view_options();

    void on_workspace_browser_group_selected(const UUID &uu_doc, const UUID &uu_group);
    void on_add_group(Group::Type group_type, WorkspaceBrowserAddGroupMode add_group_mode);
    void finish_add_group(Group *new_group);
    // as_hole: a hole (see-through, cuts with "Cut") rather than a solid
    void add_primitive(Primitive primitive, bool as_hole = false);
    // frame all solids (or the XY plane if there are none); with only_if_needed,
    // just when some of them are out of view
    void zoom_to_fit(bool only_if_needed = false);
    void add_text_object();
    class EntityText *find_text_entity(Document &doc, const UUID &body);

public:
    // looks for a newer release; interactive shows "up to date" and errors too
    void check_for_updates(bool interactive);

    // backups of unsaved work and the default folder for documents (editor_autosave.cpp)
    std::filesystem::path get_documents_dir() const;
    void init_autosave();

private:
    void show_update_banner(const struct UpdateInfo &info);
    void show_update_message(const std::string &text, bool error);
    std::filesystem::path get_autosave_dir() const;
    void autosave_now();
    // saves a small picture of the 3D view for the recent files list
    void save_thumbnail(const std::filesystem::path &document);
    void autosave_remove(const UUID &doc_uu);
    void restart_autosave_timer();
    void offer_recovery();
    sigc::connection m_autosave_connection;
    void start_update(const struct UpdateInfo &info);
    void show_object_editor(const UUID &doc_uu, const UUID &body_uu);
    // opening an object means its finished state: leaves an earlier step (picked in the list) behind
    bool go_to_last_group(const UUID &doc_uu);
    void delete_body(const UUID &doc_uu, const UUID &body_uu);
    void select_object_under_cursor();
    bool ready_made_object_under_cursor();
    void duplicate_object(const UUID &doc_uu, const UUID &body_uu);
    void cut_with_hole(const UUID &doc_uu, const UUID &hole);
    void copy_in_a_row(const UUID &doc_uu, const UUID &body_uu, unsigned int count, double gap);
    void combine_bodies(const UUID &doc_uu, const UUID &body_a, const UUID &body_b, IGroupSolidModel::Operation op);
    Gtk::DropDown *m_object_combine_dropdown = nullptr;
    struct ObjectDrag {
        UUID doc, body, group, wrkpl;
        UUID cx, cy;     // position constraints
        double x0, y0;   // their values when the drag began
        glm::dvec2 start; // cursor in workplane coordinates then
        bool moved = false;
        std::vector<PartPosition> parts; // a joined object: all its parts move instead
        glm::dvec2 last = {0, 0};          // how far they've moved
    };
    std::optional<ObjectDrag> m_object_drag;
    bool begin_object_drag(const UUID &doc_uu, const UUID &body_uu);
    void update_object_drag();
    // while dragging, slow solid models wait (Document::update_pending interactive);
    // they're built once the pointer rests for a moment
    void schedule_deferred_solid_models();
    sigc::connection m_deferred_solid_models_connection;
    void end_object_drag();
    void close_object_editor();

public:
    // resize handles on the picked object (editor_handles.cpp)
    struct ObjectHandle {
        enum class Kind { DIMENSION, WALL, ROUNDING, LIFT };
        Kind kind = Kind::DIMENSION;
        glm::dvec3 face; // where it's attached
        glm::dvec3 pos;  // where it's drawn and grabbed
        glm::dvec3 dir;  // which way is bigger
        UUID constraint, group;
        double scale = 1;  // shown value = datum * scale
        double per_mm = 1; // shown value change per mm the handle moves
        std::string label;
        double size = 0; // how big it is drawn, where that matters (Lift)
    };
    std::vector<ObjectHandle> get_object_handles();

private:
    struct HandleDrag {
        ObjectHandle handle;
        double start_value = 0, start_param = 0;
        double last_value = -1;
        GroupShell::Open open = GroupShell::Open::TOP;
        bool moved = false;
        // a part of a joined object: it and the parts like it change, the others follow
        std::vector<UUID> parts;
        std::optional<PartsFollow> follow;
        size_t dim_index = 0;
        // Kind::LIFT: where the parts were (once they have workplanes of their own, made on the first move)
        std::vector<PartPosition> lift_parts;
        double lift_start = 0; // the underside's height when the drag began
    };
    std::optional<HandleDrag> m_handle_drag;
    std::optional<ObjectHandle> get_handle_under_cursor();
    void update_handle_hint();
    std::string get_move_hint();
    std::string m_handle_hint;
    // turns the object (all its parts) about the upright line through `pivot`; `cache` keeps where the parts were
    // (filled on the first call, after they have workplanes of their own), so the angle is from the start
    void turn_object(std::vector<PartPosition> &cache, const std::vector<UUID> &parts, const glm::dvec3 &pivot,
                     double radians);
    // size lines on the picked object: shown while the pointer is over it or a drag is going on
    bool m_measure_hover = false;
    void update_measure_hover();
    // the Width/Depth/Height text under the pointer, if the lines are shown
    std::optional<std::string> get_measure_label_under_cursor();
    Gtk::SpinButton *find_object_field(const std::string &axis);
    bool click_measure_label();
    bool begin_handle_drag();
    void update_handle_drag();
    void end_handle_drag();
    struct PlacingDecal {
        UUID doc, body;
        std::shared_ptr<const class PictureData> data;
        bool repeat = false; // a pattern
    };
    void add_light(Light::Type type);
    void show_light_editor(const UUID &doc_uu, const UUID &light_uu);
    void delete_light(const UUID &doc_uu, const UUID &light_uu);
    bool pick_light_under_cursor(); // also starts dragging it (or what it's aimed at)
    struct LightDrag {
        UUID doc, light;
        bool target = false;  // dragging the aimed-at point rather than the lamp
        bool height = false;  // the dot below it: only up and down
        glm::dvec3 origin, normal; // the horizontal plane through the point (for height: the line's direction)
        glm::dvec3 start;     // where the point was
        double start_param = 0;
        glm::dvec3 offset;    // point minus where the cursor grabbed it
        bool moved = false;
    };
    std::optional<LightDrag> m_light_drag;
    void update_light_drag();
    void end_light_drag();
    std::vector<struct CanvasLight> get_canvas_lights(const Document &doc) const;
    UUID m_object_editor_light; // the panel shows this light rather than an object
    void show_render_window();
    std::unique_ptr<class RenderWindow> m_render_window;
    // builds objects from a recipe (see document/recipe.hpp) next to what's there
    bool add_from_recipe(const json &recipe, const std::string &what);
    void show_describe_window(const std::string &text = {});
    // Find (Ctrl+K), editor_search.cpp
    std::vector<struct SearchItem> get_search_items();
    void show_search(const std::string &text = {});
    void focus_object_panel_field(const std::string &label);
    std::unique_ptr<class ActionSearchWindow> m_search_window;
    std::unique_ptr<class DescribeWindow> m_describe_window;
    std::unique_ptr<class ScrewWindow> m_screw_window;
    void add_bowl();
    void add_hexagon();
    void add_triangle();
    void add_star();
    void add_heart();
    void add_half_sphere();
    void add_screw();
    void add_screw(const struct ScrewParams &params);
    void add_vase();
    void on_import_object();
    void import_object_file(const std::filesystem::path &path);
    struct GltfExportOptions get_gltf_export_options();
    void export_gltf_now(const std::filesystem::path &path);
    std::optional<PlacingDecal> m_placing_decal; // waiting for the click on the object
    void choose_picture_file(const std::string &title, std::function<void(const std::filesystem::path &)> cb);
    void add_picture_object();
    void add_picture_object(const std::filesystem::path &picture);
    void choose_decal_picture(const UUID &doc_uu, const UUID &body_uu);
    // a repeating pattern (sticker with m_repeat): then click on the object
    void begin_place_pattern(const UUID &doc_uu, const UUID &body_uu, int pattern, int colour);
    void begin_place_decal(const UUID &doc_uu, const UUID &body_uu, const std::filesystem::path &picture);
    bool place_decal_under_cursor();
    Gtk::Widget *m_object_editor = nullptr;
    // the panel is a column of cards, a grid each (or a single grid, for a light)
    std::vector<Gtk::Grid *> get_object_editor_grids() const;
    UUID m_reclicked_group; // a step clicked again (back to the last step): its release selection is ignored
    bool m_object_editor_keep = false; // skip the next refresh, the change came from the panel
    UUID m_object_editor_doc;
    UUID m_object_editor_body;
    // parts of joined objects (editor_object_parts.cpp)
    UUID m_object_editor_part;  // its handles are shown, not the whole object's
    std::set<UUID> m_split_parts; // parts edited one by one, not with the ones like them
    void append_object_parts(Gtk::Grid &grid, int &top, const UUID &doc_uu, const UUID &body_uu);
    void add_lift_row(Gtk::Grid &grid, int &top, const UUID &doc_uu, const UUID &body_uu, double height);
    std::vector<UUID> get_part_set(const Document &doc, const UUID &object, const UUID &part) const;
    bool can_move_object(const Document &doc, const UUID &body) const;
    void finish_parts_change(const UUID &doc_uu, const std::string &what);
    // The object panel is made again after changes, but only once clicks are over: taking away
    // a +/- button that's still getting clicks crashed GTK. go_to_last: first make the last group
    // the current one, so a new step (rounding, hollow) is shown.
    void schedule_object_panel_refresh(unsigned int ms = 300, bool go_to_last = false);
    // takes a panel (object panel, group editor) out of the side bar; the keyboard focus leaves
    // it first, GTK's text input could otherwise go on using a field that's gone
    void remove_from_side_panel(Gtk::Widget &panel);
    sigc::connection m_object_panel_refresh_connection;
    bool m_object_panel_go_to_last = false;
    static Gtk::SpinButton &make_dimension_spin(const class IConstraintDatum &datum, const ObjectDimension &dim);

public:
    void run_automation(const std::filesystem::path &script);

private:
    void on_delete_current_group();
    void on_move_group(Document::MoveGroup op);
    void on_workspace_browser_document_checked(const UUID &uu_doc, bool checked);
    void on_workspace_browser_group_checked(const UUID &uu_doc, const UUID &uu_group, bool checked);
    void on_workspace_browser_body_checked(const UUID &uu_doc, const UUID &uu_group, bool checked);
    void on_workspace_browser_body_solid_model_checked(const UUID &uu_doc, const UUID &uu_group, bool checked);
    void on_workspace_browser_activate_link(const std::string &link);
    void on_view_rotate(const ActionConnection &conn);
    void on_view_zoom(const ActionConnection &conn);
    void on_view_pan(const ActionConnection &conn);
    void on_view_set(const ActionConnection &conn);

    void on_workspace_browser_rename_body(const UUID &uu_doc, const UUID &uu_group);
    void on_workspace_browser_set_body_color(const UUID &uu_doc, const UUID &uu_group);
    void on_workspace_browser_reset_body_color(const UUID &uu_doc, const UUID &uu_group);
    void on_workspace_browser_move_body(const UUID &uu_doc, const UUID &uu_group, int dir);

    void on_export_solid_model(const ActionConnection &conn);
    void on_export_paths(const ActionConnection &conn);
    void on_export_projection(const ActionConnection &conn);
    void on_open_document(const ActionConnection &conn);
    void open_document_dialog(bool new_window);
    void on_save_as(const ActionConnection &conn);
    void on_create_group_action(const ActionConnection &conn);
    void on_move_group_action(const ActionConnection &conn);
    void on_align_to_workplane(const ActionConnection &conn);
    void on_center_to_workplane(const ActionConnection &conn);
    void on_look_here(const ActionConnection &conn);
    void on_go_to_group(const ActionConnection &conn);
    void on_go_to_source_group(const ActionConnection &conn);

    void on_explode_cluster(const ActionConnection &conn);
    void on_unexplode_cluster(const ActionConnection &conn);


    Canvas &get_canvas();
    const Canvas &get_canvas() const;

    Gtk::PopoverMenu *m_context_menu = nullptr;
    double m_rmb_last_x = NAN;
    double m_rmb_last_y = NAN;
    std::set<SelectableRef> m_context_menu_selection;
    enum class ContextMenuMode { ALL, CONSTRAIN };
    void open_context_menu(ContextMenuMode mode = ContextMenuMode::ALL);
    void install_hover(Gtk::Button &button, ToolID id);
    sigc::connection m_context_menu_hover_timeout;

    void set_current_group(const UUID &group);
    void canvas_update();
    void canvas_update_keep_selection();
    void render_document(const IDocumentInfo &doc);
    unsigned int m_canvas_update_pending = 0;

    class CanvasUpdater {
    public:
        [[nodiscard]] CanvasUpdater(Editor &editor);

        ~CanvasUpdater();

    private:
        Editor &m_editor;
    };

    void tool_begin(ToolID id, std::unique_ptr<ToolData> data = nullptr);
    void tool_process(ToolResponse &resp);
    void tool_process_one();
    void handle_cursor_move();
    void handle_view_changed();
    double m_last_x = NAN;
    double m_last_y = NAN;
    void handle_click(unsigned int button, unsigned int n);

    void apply_preferences();

    Gtk::Button *create_action_button(ActionToolID action);
    void attach_action_button(Gtk::Button &button, ActionToolID action);
    void attach_action_sensitive(Gtk::Widget &widget, ActionToolID action);

    Gtk::Button &create_action_bar_button(ActionToolID action, const std::string &label);
    std::map<ActionToolID, Gtk::Button *> m_action_bar_buttons;
    void update_action_bar_buttons_sensitivity();
    void update_action_bar_visibility();
    bool force_end_tool();

    Glib::RefPtr<Gio::Menu> m_view_options_menu;
    Glib::RefPtr<Gio::SimpleAction> m_perspective_action;
    void set_perspective_projection(bool persp);
    Glib::RefPtr<Gio::SimpleAction> m_previous_construction_entities_action;
    Glib::RefPtr<Gio::SimpleAction> m_show_only_solid_models_action;
    void set_show_previous_construction_entities(bool show);
    void set_show_only_solid_models(bool show);
    Glib::RefPtr<Gio::SimpleAction> m_hide_irrelevant_workplanes_action;
    void set_hide_irrelevant_workplanes(bool hide);
    Glib::RefPtr<Gio::SimpleAction> m_grid_step_action;
    void set_floor_size(double size);
    void set_grid_step(double step);
    Glib::RefPtr<Gio::SimpleAction> m_studio_lighting_action;
    void set_studio_lighting(bool studio);
    Glib::RefPtr<Gio::SimpleAction> m_snap_to_grid_action;
    void set_snap_to_grid(bool snap);
    Glib::RefPtr<Gio::SimpleAction> m_show_other_sketches_action;
    void set_show_other_sketches(bool show);
    Gtk::Window *m_grid_settings_window = nullptr;

public:
    void show_grid_settings();

private:
    void add_tool_action(ActionToolID id, const std::string &action);
    Gtk::Scale *m_curvature_comb_scale = nullptr;

    void update_view_hints();

    std::map<ActionToolID, ActionConnection> m_action_connections;

    ActionConnection &connect_action(ToolID tool_id);
    ActionConnection &connect_action(ActionToolID id, std::function<void(const ActionConnection &)> cb);
    ActionConnection &connect_action_with_source(ActionToolID id,
                                                 std::function<void(const ActionConnection &, ActionSource)> cb);

    KeySequence m_keys_current;
    KeyMatchResult keys_match(const KeySequence &keys) const;
    bool handle_typed_key(unsigned int keyval, Gdk::ModifierType state, unsigned int original_keyval);
    bool process_action_key(unsigned int keyval, Gdk::ModifierType state);
    std::pair<bool, bool> classify_keys(const KeySequence &keys) const;
    void open_find_with(std::string text);
    void flush_pending_key();
    std::string m_typed; // letters typed quickly one after another
    gint64 m_typed_at = 0;
    std::optional<std::pair<unsigned int, Gdk::ModifierType>> m_pending_key; // a shortcut waiting to see if typing follows
    sigc::connection m_pending_key_connection;
    bool handle_action_key(Glib::RefPtr<Gtk::EventControllerKey> controller, unsigned int keyval,
                           Gdk::ModifierType state);
    void handle_tool_action(const ActionConnection &conn);

    bool trigger_action(ActionToolID action, ActionSource source = ActionSource::UNKNOWN);
    bool get_action_sensitive(ActionID action) const;

    void update_action_sensitivity();
    void update_action_sensitivity(const std::set<SelectableRef> &sel);

    std::map<ActionID, bool> m_action_sensitivity;

    void tool_bar_append_action(InToolActionID action1, InToolActionID action2, InToolActionID action3,
                                const std::string &s);
    void tool_bar_clear_actions();
    std::vector<ActionLabelInfo> m_in_tool_action_label_infos;

    sigc::signal<void()> m_signal_action_sensitive;
    Preferences &m_preferences;
    InToolKeySequencesPreferences m_in_tool_key_sequeces_preferences;


    bool m_no_canvas_update = false;
    bool m_solid_model_edge_select_mode = false;

    ToolPopover *m_tool_popover = nullptr;

    WorkspaceBrowser *m_workspace_browser = nullptr;

    void update_workplane_label();
    void update_selection_mode_label();

    Gtk::Notebook *m_properties_notebook = nullptr;
    ConstraintsBox *m_constraints_box = nullptr;
    Gtk::Box *m_group_editor_box = nullptr;
    Gtk::ScrolledWindow *m_side_panel_scrolled = nullptr;
    // scrolls the side panel back to where it was once a rebuilt panel is laid out
    void restore_side_panel_scroll(double value);
    Gtk::Revealer *m_group_commit_pending_revealer = nullptr;
    GroupEditor *m_group_editor = nullptr;
    void update_group_editor();
    sigc::connection m_delayed_commit_connection;
    void commit_from_editor();
    void handle_commit_from_editor(CommitMode mode);


    SelectionEditor *m_selection_editor = nullptr;
    void update_selection_editor();
    Gtk::Revealer *m_selection_commit_pending_revealer = nullptr;
    sigc::connection m_delayed_selection_commit_connection;
    void commit_from_selection_editor();

    Dialogs m_dialogs;
    Dialogs &get_dialogs() override
    {
        return m_dialogs;
    }

    void set_constraint_icons(glm::vec3 p, glm::vec3 v, const std::vector<ConstraintType> &constraints) override;

    std::map<UUID, WorkspaceView> m_workspace_views;
    UUID m_current_workspace_view;
    WorkspaceView &get_current_workspace_view();
    std::map<UUID, DocumentView> &get_current_document_views();

    UUID create_workspace_view();
    UUID create_workspace_view_from_current();
    void set_current_workspace_view(const UUID &uu);
    void update_workspace_view_names();
    void update_workspace_tabs_visible();
    void update_can_close_workspace_view_pages();
    // "Screen 2" / "Screens 1, 2": the tabs (counted from the left) a document is shown in; empty with one tab
    std::string get_document_screens(const UUID &doc_uu);

    bool m_workspace_view_loading = false;
    void save_workspace_view(const UUID &doc_uu);
    void append_workspace_view_page(const std::string &name, const UUID &uu);
    void close_workspace_view(const UUID &uu);
    void auto_close_workspace_views();
    void rename_workspace_view(const UUID &uu);
    UUID duplicate_workspace_view(const UUID &uu);
    static std::filesystem::path get_workspace_filename_from_document_filename(const std::filesystem::path &path);

    void load_linked_documents(const UUID &uu_doc);

    DocumentView &get_current_document_view() override;

    void handle_tool_change();

    void reset_key_hint_label();

    void show_save_dialog(const std::string &doc_name, std::function<void()> save_cb, std::function<void()> no_save_cb);
    std::function<void()> m_after_save_cb;
    void close_document(const UUID &uu, std::function<void()> save_cb, std::function<void()> no_save_cb);
    void do_close_document(const UUID &uu);

    std::optional<ActionToolID> get_doubleclick_action(const SelectableRef &sr);
    ToolID get_tool_for_drag_move(bool ctrl, const std::set<SelectableRef> &sel);
    ToolID m_drag_tool;
    std::set<SelectableRef> m_selection_for_drag;
    glm::dvec2 m_cursor_pos_win_drag_begin;

    std::unique_ptr<ClippingPlaneWindow> m_clipping_plane_window;

    void update_version_info();

    Dune3DAppWindow &m_win;

    Core m_core;
    SelectionMode m_last_selection_mode;

    std::vector<ConstraintType> m_constraint_tip_icons;
    glm::vec3 m_constraint_tip_pos;
    glm::vec3 m_constraint_tip_vec;

    std::unique_ptr<SelectionFilterWindow> m_selection_filter_window;

    SelectionMenuCreator m_selection_menu_creator;

    void update_error_overlay();

    void update_title();
    UUID m_update_groups_after;
};
} // namespace dune3d

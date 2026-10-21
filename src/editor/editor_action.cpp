#include "util/util.hpp"
#include "editor.hpp"
#include "util/new_window.hpp"
#include "action/action_id.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include "workspace_browser.hpp"
#include "dune3d_appwindow.hpp"
#include "dune3d_application.hpp"
#include "preferences/preferences_window.hpp"
#include "canvas/canvas.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/entity/ientity_in_workplane_set.hpp"
#include "document/constraint/iconstraint_datum.hpp"
#include "document/constraint/constraint.hpp"
#include "document/group/igroup_solid_model.hpp"
#include "document/group/group_exploded_cluster.hpp"
#include "document/group/group_reference.hpp"
#include "document/group/igroup_source_group.hpp"
#include "document/entity/entity_cluster.hpp"
#include "util/selection_util.hpp"
#include "util/key_util.hpp"
#include "util/paths.hpp"
#include "core/tool_id.hpp"
#include "buffer.hpp"

namespace dune3d {

static const std::map<ActionID, Group::Type> create_group_action_map = {
        {ActionID::CREATE_GROUP_CHAMFER, Group::Type::CHAMFER},
        {ActionID::CREATE_GROUP_FILLET, Group::Type::FILLET},
        {ActionID::CREATE_GROUP_EXTRUDE, Group::Type::EXTRUDE},
        {ActionID::CREATE_GROUP_LATHE, Group::Type::LATHE},
        {ActionID::CREATE_GROUP_REVOLVE, Group::Type::REVOLVE},
        {ActionID::CREATE_GROUP_SKETCH, Group::Type::SKETCH},
        {ActionID::CREATE_GROUP_LOFT, Group::Type::LOFT},
        {ActionID::CREATE_GROUP_LINEAR_ARRAY, Group::Type::LINEAR_ARRAY},
        {ActionID::CREATE_GROUP_POLAR_ARRAY, Group::Type::POLAR_ARRAY},
        {ActionID::CREATE_GROUP_MIRROR_HORIZONTAL, Group::Type::MIRROR_HORIZONTAL},
        {ActionID::CREATE_GROUP_MIRROR_VERTICAL, Group::Type::MIRROR_VERTICAL},
        {ActionID::CREATE_GROUP_SOLID_MODEL_OPERATION, Group::Type::SOLID_MODEL_OPERATION},
        {ActionID::CREATE_GROUP_CLONE, Group::Type::CLONE},
        {ActionID::CREATE_GROUP_PIPE, Group::Type::PIPE},
};

static const std::map<ActionID, Document::MoveGroup> move_group_action_map = {
        {ActionID::MOVE_GROUP_UP, Document::MoveGroup::UP},
        {ActionID::MOVE_GROUP_DOWN, Document::MoveGroup::DOWN},
        {ActionID::MOVE_GROUP_TO_END_OF_BODY, Document::MoveGroup::END_OF_BODY},
        {ActionID::MOVE_GROUP_TO_END_OF_DOCUMENT, Document::MoveGroup::END_OF_DOCUMENT},
};

void Editor::init_actions()
{
    connect_action(ActionID::SAVE_ALL, [this](auto &a) {
        m_core.save_all();
        for (auto doc : m_core.get_documents()) {
            save_workspace_view(doc->get_uuid());
        }
        if (m_core.has_documents() && m_core.get_current_idocument_info().has_path())
            save_thumbnail(m_core.get_current_idocument_info().get_path());
    });
    connect_action(ActionID::SAVE, [this](auto &a) {
        if (m_core.get_current_idocument_info().has_path()) {
            if (m_core.get_needs_save())
                m_core.save();
            save_workspace_view(m_core.get_current_idocument_info().get_uuid());
            save_thumbnail(m_core.get_current_idocument_info().get_path());
            update_version_info();
            if (m_after_save_cb)
                m_after_save_cb();
            m_after_save_cb = nullptr;
        }
        else {
            trigger_action(ActionID::SAVE_AS);
        }
    });

    connect_action(ActionID::SAVE_AS, sigc::mem_fun(*this, &Editor::on_save_as));


    connect_action(ActionID::CLOSE_DOCUMENT, [this](auto &a) {
        auto &doci = m_core.get_current_idocument_info();
        close_document(doci.get_uuid(), nullptr, nullptr);
    });


    connect_action(ActionID::NEW_DOCUMENT, [this](auto &a) {
        auto wsv = create_workspace_view();
        auto uu = m_core.add_document();
        auto &dv = m_workspace_views.at(wsv).m_documents[uu];
        dv.m_document_is_visible = true;
        dv.m_current_group = m_core.get_idocument_info(uu).get_current_group();
        m_workspace_views.at(wsv).m_current_document = uu;
        set_current_workspace_view(wsv);
        update_title();
        update_workspace_view_names();
    });
    connect_action(ActionID::OPEN_DOCUMENT, sigc::mem_fun(*this, &Editor::on_open_document));
    // the look of the picked object: take you to its colour, material and sticker settings in the panel
    connect_action(ActionID::FOCUS_COLOUR, [this](auto &a) { focus_object_panel_field("Colour"); });
    connect_action(ActionID::FOCUS_MATERIAL, [this](auto &a) { focus_object_panel_field("Material"); });
    connect_action(ActionID::FOCUS_STICKER, [this](auto &a) { focus_object_panel_field("Add sticker..."); });
    connect_action(ActionID::IMPORT_OBJECT, [this](auto &a) { on_import_object(); });
    connect_action(ActionID::NEW_WINDOW, [this](auto &a) {
        if (!launch_new_window())
            Logger::log_warning("Couldn't start a new window", Logger::Domain::EDITOR);
    });
    connect_action(ActionID::OPEN_DOCUMENT_NEW_WINDOW, [this](auto &a) { open_document_dialog(true); });


    connect_action(ActionID::EXPORT_SOLID_MODEL_STL, sigc::mem_fun(*this, &Editor::on_export_solid_model));
    connect_action(ActionID::EXPORT_SOLID_MODEL_STEP, sigc::mem_fun(*this, &Editor::on_export_solid_model));
    connect_action(ActionID::EXPORT_ALL_SOLID_MODELS_STEP, sigc::mem_fun(*this, &Editor::on_export_solid_model));
    connect_action(ActionID::EXPORT_ALL_SOLID_MODELS_STL, sigc::mem_fun(*this, &Editor::on_export_solid_model));
    connect_action(ActionID::EXPORT_ALL_GLTF, sigc::mem_fun(*this, &Editor::on_export_solid_model));

    connect_action(ActionID::NEXT_GROUP, [this](auto &a) { m_workspace_browser->group_prev_next(1); });
    connect_action(ActionID::PREVIOUS_GROUP, [this](auto &a) { m_workspace_browser->group_prev_next(-1); });

    connect_action(ActionID::TOGGLE_SOLID_MODEL, [this](const auto &a) {
        CanvasUpdater canvas_updater{*this};
        auto &doc = m_core.get_current_document();
        auto &group = doc.get_group(m_core.get_current_group());
        auto &body_group = group.find_body(doc).group.m_uuid;
        auto &doc_view = get_current_document_view();

        doc_view.m_body_views[body_group].m_solid_model_visible = !doc_view.body_solid_model_is_visible(body_group);
        m_workspace_browser->update_current_group(get_current_document_views());
    });

    connect_action(ActionID::TOGGLE_SHOW_ONLY_SOLID_MODEL, [this](const auto &a) {
        if (!m_core.has_documents())
            return;
        set_show_only_solid_models(!get_current_workspace_view().m_show_only_solid_models);
    });

    for (const auto &[id, it] : action_catalog) {
        if (std::holds_alternative<ToolID>(id)) {
            connect_action(std::get<ToolID>(id));
        }
    }

    connect_action(ActionID::UNDO, [this](const auto &a) {
        m_core.undo();
        m_win.hide_delete_items_popup();
        CanvasUpdater canvas_updater{*this};
        update_workplane_label();
        update_selection_editor();
        update_action_sensitivity();
    });
    connect_action(ActionID::REDO, [this](const auto &a) {
        m_core.redo();
        m_win.hide_delete_items_popup();
        CanvasUpdater canvas_updater{*this};
        update_workplane_label();
        update_selection_editor();
        update_action_sensitivity();
    });

    connect_action(ActionID::PREFERENCES, [this](const auto &a) {
        auto pwin = dynamic_cast<Dune3DApplication &>(*m_win.get_application()).show_preferences_window();
        pwin->set_transient_for(m_win);
    });


    connect_action(ActionID::VIEW_ALL, [this](auto &a) {
        get_canvas().set_cam_quat(glm::quat_identity<float, glm::defaultp>());
        get_canvas().set_cam_distance(10, Canvas::ZoomCenter::CURSOR);
        get_canvas().set_center({0, 0, 0});
    });

    connect_action(ActionID::VIEW_RESET_TILT, [this](auto &a) {
        const auto q = get_canvas().get_cam_quat();
        const auto z = glm::rotate(glm::inverse(q), glm::vec3(0, 0, 1));
        const auto phi = atan2(z.y, z.x);
        const auto ry = glm::angleAxis(-(float)(M_PI / 2 - phi), glm::rotate(q, glm::vec3(0, 0, 1)));
        get_canvas().animate_to_cam_quat(ry * q);
    });

    connect_action(ActionID::ALIGN_VIEW_TO_WORKPLANE, sigc::mem_fun(*this, &Editor::on_align_to_workplane));
    connect_action(ActionID::ALIGN_VIEW_TO_CURRENT_WORKPLANE, sigc::mem_fun(*this, &Editor::on_align_to_workplane));
    connect_action(ActionID::CENTER_VIEW_TO_WORKPLANE, sigc::mem_fun(*this, &Editor::on_center_to_workplane));
    connect_action(ActionID::CENTER_VIEW_TO_CURRENT_WORKPLANE, sigc::mem_fun(*this, &Editor::on_center_to_workplane));
    connect_action(ActionID::LOOK_HERE, sigc::mem_fun(*this, &Editor::on_look_here));
    connect_action(ActionID::ALIGN_AND_CENTER_VIEW_TO_WORKPLANE, [this](const auto &a) {
        trigger_action(ActionID::ALIGN_VIEW_TO_WORKPLANE);
        trigger_action(ActionID::CENTER_VIEW_TO_WORKPLANE);
    });

    connect_action(ActionID::GO_TO_GROUP, sigc::mem_fun(*this, &Editor::on_go_to_group));
    connect_action(ActionID::GO_TO_SOURCE_GROUP, sigc::mem_fun(*this, &Editor::on_go_to_source_group));


    connect_action(ActionID::VIEW_PERSP, [this](auto &a) { set_perspective_projection(true); });
    connect_action(ActionID::VIEW_ORTHO, [this](auto &a) { set_perspective_projection(false); });
    connect_action(ActionID::VIEW_TOGGLE_PERSP_ORTHO, [this](auto &a) {
        set_perspective_projection(get_canvas().get_projection() == Canvas::Projection::ORTHO);
    });

    connect_action(ActionID::VIEW_ROTATE_UP, sigc::mem_fun(*this, &Editor::on_view_rotate));
    connect_action(ActionID::VIEW_ROTATE_DOWN, sigc::mem_fun(*this, &Editor::on_view_rotate));
    connect_action(ActionID::VIEW_ROTATE_LEFT, sigc::mem_fun(*this, &Editor::on_view_rotate));
    connect_action(ActionID::VIEW_ROTATE_RIGHT, sigc::mem_fun(*this, &Editor::on_view_rotate));

    connect_action(ActionID::VIEW_TILT_LEFT, sigc::mem_fun(*this, &Editor::on_view_rotate));
    connect_action(ActionID::VIEW_TILT_RIGHT, sigc::mem_fun(*this, &Editor::on_view_rotate));

    connect_action(ActionID::VIEW_FIT, [this](auto &a) { zoom_to_fit(); });
    m_core.signal_documents_changed().connect([this] {
        static size_t n_docs = 0;
        const auto n = m_core.get_documents().size();
        if (n > n_docs && m_core.has_documents() && !m_core.get_current_idocument_info().has_path()) {
            Glib::signal_idle().connect_once([this] {
                get_canvas().set_cam_quat(glm::quatLookAt(glm::normalize(glm::vec3(-1, 1.3, -.9)), glm::vec3(0, 0, 1)));
                zoom_to_fit();
            });
        }
        n_docs = n;
    });
    connect_action(ActionID::VIEW_ISO, sigc::mem_fun(*this, &Editor::on_view_set));
    connect_action(ActionID::VIEW_FRONT, sigc::mem_fun(*this, &Editor::on_view_set));
    connect_action(ActionID::VIEW_BACK, sigc::mem_fun(*this, &Editor::on_view_set));
    connect_action(ActionID::VIEW_TOP, sigc::mem_fun(*this, &Editor::on_view_set));
    connect_action(ActionID::VIEW_BOTTOM, sigc::mem_fun(*this, &Editor::on_view_set));
    connect_action(ActionID::VIEW_LEFT, sigc::mem_fun(*this, &Editor::on_view_set));
    connect_action(ActionID::VIEW_RIGHT, sigc::mem_fun(*this, &Editor::on_view_set));

    connect_action(ActionID::VIEW_ZOOM_IN, sigc::mem_fun(*this, &Editor::on_view_zoom));
    connect_action(ActionID::VIEW_ZOOM_OUT, sigc::mem_fun(*this, &Editor::on_view_zoom));

    connect_action(ActionID::VIEW_PAN_UP, sigc::mem_fun(*this, &Editor::on_view_pan));
    connect_action(ActionID::VIEW_PAN_DOWN, sigc::mem_fun(*this, &Editor::on_view_pan));
    connect_action(ActionID::VIEW_PAN_LEFT, sigc::mem_fun(*this, &Editor::on_view_pan));
    connect_action(ActionID::VIEW_PAN_RIGHT, sigc::mem_fun(*this, &Editor::on_view_pan));

    connect_action(ActionID::DELETE_CURRENT_GROUP, [this](auto &a) { on_delete_current_group(); });

    for (const auto [act, group_type] : create_group_action_map) {
        connect_action(act, sigc::mem_fun(*this, &Editor::on_create_group_action));
    }
    connect_action(ActionID::ADD_BOX, [this](auto &a) { add_primitive(Primitive::BOX); });
    connect_action(ActionID::ADD_CYLINDER, [this](auto &a) { add_primitive(Primitive::CYLINDER); });
    connect_action(ActionID::ADD_SPHERE, [this](auto &a) { add_primitive(Primitive::SPHERE); });
    connect_action(ActionID::ADD_CONE, [this](auto &a) { add_primitive(Primitive::CONE); });
    connect_action(ActionID::ADD_TUBE, [this](auto &a) { add_primitive(Primitive::TUBE); });
    connect_action(ActionID::ADD_TORUS, [this](auto &a) { add_primitive(Primitive::TORUS); });
    connect_action(ActionID::ADD_WEDGE, [this](auto &a) { add_primitive(Primitive::WEDGE); });
    connect_action(ActionID::ADD_HEXAGON, [this](auto &a) { add_hexagon(); });
    connect_action(ActionID::ADD_TRIANGLE, [this](auto &a) { add_triangle(); });
    connect_action(ActionID::ADD_STAR, [this](auto &a) { add_star(); });
    connect_action(ActionID::ADD_HEART, [this](auto &a) { add_heart(); });
    connect_action(ActionID::ADD_HALF_SPHERE, [this](auto &a) { add_half_sphere(); });
    connect_action(ActionID::ADD_SCREW, [this](auto &a) { add_screw(); });
    connect_action(ActionID::ADD_HOLE_SHAPE, [this](auto &a) { add_primitive(Primitive::CYLINDER, true); });
    connect_action(ActionID::ADD_TEXT, [this](auto &a) { add_text_object(); });
    connect_action(ActionID::ADD_PICTURE_OBJECT, [this](auto &a) { add_picture_object(); });
    connect_action(ActionID::ADD_LAMP, [this](auto &a) { add_light(Light::Type::POINT); });
    connect_action(ActionID::ADD_BOWL, [this](auto &a) { add_bowl(); });
    connect_action(ActionID::MAKE_FROM_DESCRIPTION, [this](auto &a) { show_describe_window(); });
    connect_action(ActionID::FIND, [this](auto &a) { show_search(); });
    connect_action(ActionID::ADD_VASE, [this](auto &a) { add_vase(); });
    connect_action(ActionID::ADD_SPOTLIGHT, [this](auto &a) { add_light(Light::Type::SPOT); });
    connect_action(ActionID::ADD_SUN, [this](auto &a) { add_light(Light::Type::SUN); });
    connect_action(ActionID::DUPLICATE_OBJECT, [this](auto &a) {
        if (m_object_editor)
            duplicate_object(m_object_editor_doc, m_object_editor_body);
        else
            m_workspace_browser->show_toast("Click an object first, then duplicate it");
    });
    connect_action(ActionID::COMBINE_OBJECTS, [this](auto &a) {
        if (m_object_combine_dropdown)
            m_object_combine_dropdown->grab_focus();
        else
            m_workspace_browser->show_toast("Click an object first,\nthen pick what to combine it with");
    });
    for (const auto [act, move] : move_group_action_map) {
        connect_action(act, sigc::mem_fun(*this, &Editor::on_move_group_action));
    }
    connect_action(ActionID::TOGGLE_WORKPLANE, [this](auto &a) {
        auto &cb = m_win.get_workplane_checkbutton();
        cb.set_active(!cb.get_active());
    });
    m_win.get_workplane_checkbutton().signal_toggled().connect([this] { update_action_bar_buttons_sensitivity(); });

    connect_action(ActionID::SELECT_PATH, [this](auto &a) {
        auto &doc = m_core.get_current_document();

        auto enp = point_from_selection(doc, get_canvas().get_selection());
        if (!enp)
            return;
        if (enp->point != 0)
            return;
        auto &en = doc.get_entity(enp->entity);
        auto &group = doc.get_group(en.m_group);
        auto en_wrkpl = dynamic_cast<const IEntityInWorkplane *>(&en);
        if (!en_wrkpl)
            return;
        auto paths =
                paths::Paths::from_document(m_core.get_current_document(), en_wrkpl->get_workplane(), group.m_uuid);

        for (auto &path : paths.paths) {
            for (auto &[node, edge] : path) {
                if (edge.entity.m_uuid == en.m_uuid) {
                    std::set<SelectableRef> sel;
                    for (auto &[node2, edge2] : path) {
                        sel.emplace(SelectableRef::Type::ENTITY, edge2.entity.m_uuid, 0);
                    }
                    get_canvas().set_selection(sel, true);
                    get_canvas().set_selection_mode(SelectionMode::NORMAL);
                    return;
                }
            }
        }
    });

    connect_action(ActionID::EXPORT_PATHS, sigc::mem_fun(*this, &Editor::on_export_paths));
    connect_action(ActionID::EXPORT_PATHS_IN_CURRENT_GROUP, sigc::mem_fun(*this, &Editor::on_export_paths));
    connect_action(ActionID::EXPORT_DXF_CURRENT_GROUP, sigc::mem_fun(*this, &Editor::on_export_paths));
    connect_action(ActionID::EXPORT_PROJECTION, sigc::mem_fun(*this, &Editor::on_export_projection));
    connect_action(ActionID::EXPORT_PROJECTION_ALL, sigc::mem_fun(*this, &Editor::on_export_projection));

    connect_action(ActionID::SELECT_ALL_ENTITIES_IN_CURRENT_GROUP, [this](auto &a) {
        auto &doc = m_core.get_current_document();
        auto group = m_core.get_current_group();
        std::set<SelectableRef> sel;
        for (auto &[uu, en] : doc.m_entities) {
            if (en->m_group == group)
                sel.emplace(SelectableRef::Type::ENTITY, uu, 0);
        }
        get_canvas().set_selection(sel, true);
        get_canvas().set_selection_mode(SelectionMode::NORMAL);
        return;
    });

    connect_action(ActionID::SET_CURRENT_DOCUMENT, [this](const auto &a) {
        if (auto doc = document_from_selection(get_canvas().get_selection())) {
            CanvasUpdater canvas_updater{*this};
            m_core.set_current_document(doc.value());
            m_workspace_views.at(m_current_workspace_view).m_current_document = doc.value();
            m_workspace_browser->update_current_group(get_current_document_views());
            update_version_info();
        }
    });

    connect_action(ActionID::EXPLODE_CLUSTER, sigc::mem_fun(*this, &Editor::on_explode_cluster));
    connect_action(ActionID::UNEXPLODE_CLUSTER, sigc::mem_fun(*this, &Editor::on_unexplode_cluster));
    connect_action(ActionID::TOGGLE_PREVIOUS_CONSTRUCTION_ENTITIES, [this](const auto &conn) {
        if (!m_core.has_documents())
            return;
        set_show_previous_construction_entities(
                !get_current_workspace_view().m_show_construction_entities_from_previous_groups);
    });
    connect_action(ActionID::TOGGLE_IRRELEVANT_WORKPLANES, [this](const auto &conn) {
        if (!m_core.has_documents())
            return;
        set_hide_irrelevant_workplanes(!get_current_workspace_view().m_hide_irrelevant_workplanes);
    });

    connect_action(ActionID::COPY, [this](const auto &conn) {
        if (auto buf = Buffer::create(m_core.get_current_document(), get_canvas().get_selection(),
                                      Buffer::Operation::COPY))
            set_buffer(std::move(buf));
    });

    connect_action(ActionID::CONSTRAIN_MENU,
                   [this](const auto &conn) { open_context_menu(ContextMenuMode::CONSTRAIN); });

    m_core.signal_rebuilt().connect([this] { update_action_sensitivity(); });
    m_core.signal_rebuilt().connect([this] {
        if (!m_current_workspace_view)
            return;
        if (!m_core.has_documents())
            return;
        get_current_document_view().m_current_group = m_core.get_current_group();
    });
    m_core.signal_rebuilt().connect([this] { load_linked_documents(m_core.get_current_idocument_info().get_uuid()); });
}

void Editor::set_perspective_projection(bool persp)
{
    using P = Canvas::Projection;
    get_canvas().set_projection(persp ? P::PERSP : P::ORTHO);
    m_perspective_action->set_state(Glib::Variant<bool>::create(persp));
    update_view_hints();
}

void Editor::set_show_previous_construction_entities(bool show)
{
    if (!m_core.has_documents())
        return;
    CanvasUpdater canvas_updater{*this};
    get_current_workspace_view().m_show_construction_entities_from_previous_groups = show;
    m_previous_construction_entities_action->set_state(Glib::Variant<bool>::create(show));
    update_view_hints();
}

void Editor::set_show_only_solid_models(bool show)
{
    if (!m_core.has_documents())
        return;
    CanvasUpdater canvas_updater{*this};
    get_current_workspace_view().m_show_only_solid_models = show;
    m_show_only_solid_models_action->set_state(Glib::Variant<bool>::create(show));
    update_view_hints();
}

void Editor::set_hide_irrelevant_workplanes(bool hide)
{
    if (!m_core.has_documents())
        return;
    CanvasUpdater canvas_updater{*this};
    get_current_workspace_view().m_hide_irrelevant_workplanes = hide;
    m_hide_irrelevant_workplanes_action->set_state(Glib::Variant<bool>::create(hide));
    update_view_hints();
}


void Editor::set_show_other_sketches(bool show)
{
    CanvasUpdater canvas_updater{*this};
    if (m_core.has_documents())
        get_current_workspace_view().m_show_other_sketches = show;
    m_show_other_sketches_action->set_state(Glib::Variant<bool>::create(show));
}

void Editor::set_snap_to_grid(bool snap)
{
    if (m_core.has_documents())
        get_current_workspace_view().m_snap_to_grid = snap;
    m_snap_to_grid_action->set_state(Glib::Variant<bool>::create(snap));
    update_workplane_label();
}

void Editor::set_studio_lighting(bool studio)
{
    if (m_core.has_documents())
        get_current_workspace_view().m_studio_lighting = studio;
    get_canvas().set_studio_lighting(studio);
    m_studio_lighting_action->set_state(Glib::Variant<bool>::create(studio));
}

void Editor::set_grid_step(double step)
{
    if (!m_core.has_documents())
        return;
    CanvasUpdater canvas_updater{*this};
    get_current_workspace_view().m_grid_step = step;
    m_grid_step_action->set_state(Glib::Variant<Glib::ustring>::create(format_grid_step(step, false)));
    update_workplane_label();
}

void Editor::on_create_group_action(const ActionConnection &conn)
{
    on_add_group(create_group_action_map.at(std::get<ActionID>(conn.id)), WorkspaceBrowserAddGroupMode::WITHOUT_BODY);
}

void Editor::on_move_group_action(const ActionConnection &conn)
{
    on_move_group(move_group_action_map.at(std::get<ActionID>(conn.id)));
}

void Editor::on_align_to_workplane(const ActionConnection &conn)
{
    if (!m_core.has_documents())
        return;
    UUID wrkpl_uu;
    const auto action = std::get<ActionID>(conn.id);

    if (action == ActionID::ALIGN_VIEW_TO_CURRENT_WORKPLANE) {
        wrkpl_uu = m_core.get_current_workplane();
    }
    else {
        if (auto wrkpl_opt = point_from_selection(m_core.get_current_document(), get_canvas().get_selection(),
                                                  Entity::Type::WORKPLANE))
            wrkpl_uu = wrkpl_opt->entity;
    }
    if (!wrkpl_uu)
        return;

    auto &wrkpl = m_core.get_current_document().get_entity<EntityWorkplane>(wrkpl_uu);
    get_canvas().animate_to_cam_quat(wrkpl.m_normal);
}

void Editor::on_center_to_workplane(const ActionConnection &conn)
{
    if (!m_core.has_documents())
        return;
    UUID wrkpl_uu;
    const auto action = std::get<ActionID>(conn.id);

    if (action == ActionID::CENTER_VIEW_TO_CURRENT_WORKPLANE) {
        wrkpl_uu = m_core.get_current_workplane();
    }
    else {
        if (auto wrkpl_opt = point_from_selection(m_core.get_current_document(), get_canvas().get_selection(),
                                                  Entity::Type::WORKPLANE))
            wrkpl_uu = wrkpl_opt->entity;
    }
    if (!wrkpl_uu)
        return;

    auto &wrkpl = m_core.get_current_document().get_entity<EntityWorkplane>(wrkpl_uu);
    get_canvas().animate_to_center_abs(wrkpl.m_origin);
}

void Editor::on_look_here(const ActionConnection &conn)
{
    if (!m_core.has_documents())
        return;
    auto enp = point_from_selection(m_core.get_current_document(), get_canvas().get_selection());
    if (!enp)
        return;

    auto &doc = m_core.get_current_document();
    if (!doc.is_valid_point(*enp))
        return;

    auto pt = doc.get_point(*enp);

    get_canvas().animate_to_center_abs(pt);
}

void Editor::on_go_to_group(const ActionConnection &conn)
{
    if (!m_core.has_documents())
        return;
    auto enp = point_from_selection(m_core.get_current_document(), get_canvas().get_selection());
    if (!enp)
        return;

    const auto &doc = m_core.get_current_document();
    auto &en = doc.get_entity(enp->entity);

    m_workspace_browser->select_group(en.m_group);
}

void Editor::on_go_to_source_group(const ActionConnection &conn)
{
    if (!m_core.has_documents())
        return;

    const auto &doc = m_core.get_current_document();
    auto &group = doc.get_group(m_core.get_current_group());

    auto sg = dynamic_cast<const IGroupSourceGroup *>(&group);
    if (!sg)
        return;

    auto sources = sg->get_source_groups(doc);
    if (sources.size() != 1)
        return;

    m_workspace_browser->select_group(*sources.begin());
}


void Editor::update_action_sensitivity()
{
    update_action_sensitivity(get_canvas().get_selection());
}

void Editor::update_action_sensitivity(const std::set<SelectableRef> &sel)
{
    m_action_sensitivity[ActionID::UNDO] = m_core.can_undo();
    m_action_sensitivity[ActionID::REDO] = m_core.can_redo();
    m_action_sensitivity[ActionID::SAVE_ALL] = m_core.get_needs_save_any();
    m_action_sensitivity[ActionID::SAVE] = m_core.has_documents();
    m_action_sensitivity[ActionID::SAVE_AS] = m_core.has_documents();
    m_action_sensitivity[ActionID::CLOSE_DOCUMENT] = m_core.has_documents();
    m_action_sensitivity[ActionID::OPEN_DOCUMENT] = true;

    m_action_sensitivity[ActionID::TOGGLE_SOLID_MODEL] = m_core.has_documents();
    m_action_sensitivity[ActionID::TOGGLE_SHOW_ONLY_SOLID_MODEL] = m_core.has_documents();
    m_action_sensitivity[ActionID::NEW_DOCUMENT] = true;
    m_action_sensitivity[ActionID::NEW_WINDOW] = true;
    m_action_sensitivity[ActionID::IMPORT_OBJECT] = true;
    for (auto act : {ActionID::FOCUS_COLOUR, ActionID::FOCUS_MATERIAL, ActionID::FOCUS_STICKER})
        m_action_sensitivity[act] = true;
    m_action_sensitivity[ActionID::OPEN_DOCUMENT_NEW_WINDOW] = true;
    m_action_sensitivity[ActionID::FIND] = true;
    for (auto act : {ActionID::ADD_BOX, ActionID::ADD_CYLINDER, ActionID::ADD_SPHERE, ActionID::ADD_CONE, ActionID::ADD_TUBE,
                     ActionID::ADD_TORUS, ActionID::ADD_WEDGE, ActionID::ADD_HEXAGON, ActionID::ADD_TRIANGLE,
                     ActionID::ADD_STAR, ActionID::ADD_HEART, ActionID::ADD_HALF_SPHERE, ActionID::ADD_TEXT,
                     ActionID::ADD_PICTURE_OBJECT, ActionID::COMBINE_OBJECTS,
                     ActionID::ADD_TORUS, ActionID::ADD_WEDGE, ActionID::ADD_SCREW, ActionID::ADD_TEXT, ActionID::COMBINE_OBJECTS,
                     ActionID::ADD_TORUS, ActionID::ADD_WEDGE, ActionID::ADD_HOLE_SHAPE, ActionID::ADD_TEXT, ActionID::COMBINE_OBJECTS,
                     ActionID::ADD_LAMP, ActionID::ADD_SPOTLIGHT, ActionID::ADD_SUN, ActionID::ADD_BOWL,
                     ActionID::ADD_VASE, ActionID::MAKE_FROM_DESCRIPTION,
                     ActionID::DUPLICATE_OBJECT})
        m_action_sensitivity[act] = m_core.has_documents();
    m_action_sensitivity[ActionID::TOGGLE_WORKPLANE] = m_core.has_documents();
    m_action_sensitivity[ActionID::SELECT_UNDERCONSTRAINED] = m_core.has_documents();
    m_action_sensitivity[ActionID::SELECT_ALL_ENTITIES_IN_CURRENT_GROUP] = m_core.has_documents();
    m_action_sensitivity[ActionID::TOGGLE_PREVIOUS_CONSTRUCTION_ENTITIES] = m_core.has_documents();
    m_action_sensitivity[ActionID::TOGGLE_IRRELEVANT_WORKPLANES] = m_core.has_documents();
    m_action_sensitivity[ActionID::EXPORT_PROJECTION_ALL] = m_core.has_documents();
    bool has_solid_model = false;

    for (const auto [act, group_type] : create_group_action_map) {
        m_action_sensitivity[act] = m_core.has_documents();
    }

    for (const auto [act, move] : move_group_action_map) {
        m_action_sensitivity[act] = m_core.has_documents();
    }
    if (m_core.has_documents()) {
        auto &current_group = m_core.get_current_document().get_group(m_core.get_current_group());

        if (auto gr_solid = dynamic_cast<const IGroupSolidModel *>(&current_group))
            has_solid_model = gr_solid->get_solid_model() != nullptr;

        {
            bool can_go_to_source_group = false;
            if (auto gr_source = dynamic_cast<const IGroupSourceGroup *>(&current_group)) {
                auto srcs = gr_source->get_source_groups(m_core.get_current_document());
                can_go_to_source_group = srcs.size() == 1;
            }
            m_action_sensitivity[ActionID::GO_TO_SOURCE_GROUP] = can_go_to_source_group;
        }

        auto groups_sorted = m_core.get_current_document().get_groups_sorted();
        assert(groups_sorted.size());
        const bool is_first = groups_sorted.front() == &current_group;
        const bool is_last = groups_sorted.back() == &current_group;
        m_action_sensitivity[ActionID::PREVIOUS_GROUP] = !is_first;
        m_action_sensitivity[ActionID::NEXT_GROUP] = !is_last;
        m_action_sensitivity[ActionID::DELETE_CURRENT_GROUP] = !is_first;
        const auto has_current_wrkpl = current_group.m_active_wrkpl != UUID();
        m_action_sensitivity[ActionID::CREATE_GROUP_EXTRUDE] = has_current_wrkpl;
        m_action_sensitivity[ActionID::CREATE_GROUP_LATHE] = has_current_wrkpl;
        for (const auto &[act, mg] : move_group_action_map) {
            m_action_sensitivity[act] =
                    m_core.get_current_document().get_group_after(current_group.m_uuid, mg) != UUID();
        }
        m_action_sensitivity[ActionID::ALIGN_VIEW_TO_CURRENT_WORKPLANE] = has_current_wrkpl;
        m_action_sensitivity[ActionID::CENTER_VIEW_TO_CURRENT_WORKPLANE] = has_current_wrkpl;
        const auto sel_is_workplane =
                point_from_selection(m_core.get_current_document(), sel, Entity::Type::WORKPLANE).has_value();
        m_action_sensitivity[ActionID::ALIGN_VIEW_TO_WORKPLANE] = sel_is_workplane;
        m_action_sensitivity[ActionID::CENTER_VIEW_TO_WORKPLANE] = sel_is_workplane;
        m_action_sensitivity[ActionID::ALIGN_AND_CENTER_VIEW_TO_WORKPLANE] = sel_is_workplane;
        {
            auto enp = point_from_selection(m_core.get_current_document(), sel);
            bool can_select_path = false;
            bool can_look_here = false;
            bool can_go_to_group = false;

            if (enp) {
                auto &en = m_core.get_current_document().get_entity(enp->entity);
                can_go_to_group = en.m_group != current_group.m_uuid;
                can_select_path = en.of_type(Entity::Type::LINE_2D, Entity::Type::ARC_2D, Entity::Type::BEZIER_2D);

                can_look_here = m_core.get_current_document().is_valid_point(*enp);
            }
            m_action_sensitivity[ActionID::SELECT_PATH] = can_select_path;
            m_action_sensitivity[ActionID::LOOK_HERE] = can_look_here;
            m_action_sensitivity[ActionID::GO_TO_GROUP] = can_go_to_group;
        }

        m_action_sensitivity[ActionID::EXPORT_PATHS] = has_current_wrkpl;
        m_action_sensitivity[ActionID::EXPORT_PATHS_IN_CURRENT_GROUP] = has_current_wrkpl;
        m_action_sensitivity[ActionID::EXPORT_DXF_CURRENT_GROUP] = has_current_wrkpl;
        m_action_sensitivity[ActionID::SET_CURRENT_DOCUMENT] = document_from_selection(sel).has_value();
        {
            auto enp = point_from_selection(m_core.get_current_document(), sel, EntityType::CLUSTER);
            bool can_explode_cluster = false;
            if (enp) {
                auto &en = m_core.get_current_document().get_entity<EntityCluster>(enp->entity);
                can_explode_cluster = !en.m_exploded_group;
            }
            m_action_sensitivity[ActionID::EXPLODE_CLUSTER] = can_explode_cluster;
        }

        m_action_sensitivity[ActionID::UNEXPLODE_CLUSTER] = current_group.get_type() == Group::Type::EXPLODED_CLUSTER;
        m_action_sensitivity[ActionID::COPY] = Buffer::can_create(m_core.get_current_document(), sel);
    }
    else {
        m_action_sensitivity[ActionID::PREVIOUS_GROUP] = false;
        m_action_sensitivity[ActionID::NEXT_GROUP] = false;
        m_action_sensitivity[ActionID::DELETE_CURRENT_GROUP] = false;
        m_action_sensitivity[ActionID::ALIGN_VIEW_TO_WORKPLANE] = false;
        m_action_sensitivity[ActionID::ALIGN_VIEW_TO_CURRENT_WORKPLANE] = false;
        m_action_sensitivity[ActionID::CENTER_VIEW_TO_WORKPLANE] = false;
        m_action_sensitivity[ActionID::CENTER_VIEW_TO_CURRENT_WORKPLANE] = false;
        m_action_sensitivity[ActionID::LOOK_HERE] = false;
        m_action_sensitivity[ActionID::SELECT_PATH] = false;
        m_action_sensitivity[ActionID::EXPORT_PATHS] = false;
        m_action_sensitivity[ActionID::EXPORT_PATHS_IN_CURRENT_GROUP] = false;
        m_action_sensitivity[ActionID::EXPORT_DXF_CURRENT_GROUP] = false;
        m_action_sensitivity[ActionID::SET_CURRENT_DOCUMENT] = false;
        m_action_sensitivity[ActionID::EXPLODE_CLUSTER] = false;
        m_action_sensitivity[ActionID::UNEXPLODE_CLUSTER] = false;
        m_action_sensitivity[ActionID::COPY] = false;
        m_action_sensitivity[ActionID::GO_TO_GROUP] = false;
        m_action_sensitivity[ActionID::GO_TO_SOURCE_GROUP] = false;
    }

    m_action_sensitivity[ActionID::EXPORT_SOLID_MODEL_STEP] = has_solid_model;
    m_action_sensitivity[ActionID::EXPORT_SOLID_MODEL_STL] = has_solid_model;
    m_action_sensitivity[ActionID::EXPORT_PROJECTION] = has_solid_model;
    m_action_sensitivity[ActionID::EXPORT_ALL_SOLID_MODELS_STEP] = m_core.has_documents();
    m_action_sensitivity[ActionID::EXPORT_ALL_SOLID_MODELS_STL] = m_core.has_documents();
    m_action_sensitivity[ActionID::EXPORT_ALL_GLTF] = m_core.has_documents();


    m_signal_action_sensitive.emit();
}


Gtk::Button *Editor::create_action_button(ActionToolID action)
{
    auto &catitem = action_catalog.at(action);
    auto button = Gtk::manage(new Gtk::Button(catitem.name.full));
    attach_action_button(*button, action);
    return button;
}

bool Editor::trigger_action(ActionToolID action, ActionSource source)
{
    // Delete with an object picked and nothing else selected deletes the object
    if (action == ActionToolID(ToolID::DELETE) && m_object_editor && m_object_editor_light
        && !m_core.tool_is_active() && get_canvas().get_selection().empty()) {
        const auto doc = m_object_editor_doc, light = m_object_editor_light;
        Glib::signal_idle().connect_once([this, doc, light] { delete_light(doc, light); });
        return true;
    }
    if (action == ActionToolID(ToolID::DELETE) && m_object_editor && !m_core.tool_is_active()
        && get_canvas().get_selection().empty()) {
        const auto doc = m_object_editor_doc, body = m_object_editor_body;
        Glib::signal_idle().connect_once([this, doc, body] { delete_body(doc, body); });
        return true;
    }
    if (m_core.tool_is_active() && !(action_catalog.at(action).flags & ActionCatalogItem::FLAGS_IN_TOOL)) {
        return false;
    }
    if (std::holds_alternative<ActionID>(action) && !get_action_sensitive(std::get<ActionID>(action)))
        return false;
    /*main_window->key_hint_set_visible(false);
    if (keys_current.size()) {
        keys_current.clear();
        reset_tool_hint_label();
    }*/
    auto conn = m_action_connections.at(action);
    conn.cb(conn, source);
    return true;
}

bool Editor::get_action_sensitive(ActionID action) const
{
    if (!m_action_connections.count(action)) // actions not connected can't be sensitive
        return false;

    if (m_core.tool_is_active()) // actions available int tools are always sensitive
        return action_catalog.at(action).flags & ActionCatalogItem::FLAGS_IN_TOOL;

    if (m_action_sensitivity.count(action))
        return m_action_sensitivity.at(action);
    else
        return true;
}

void Editor::attach_action_button(Gtk::Button &button, ActionToolID action)
{
    button.signal_clicked().connect([this, action] { trigger_action(action); });
    attach_action_sensitive(button, action);
}

void Editor::attach_action_sensitive(Gtk::Widget &widget, ActionToolID action)
{
    if (std::holds_alternative<ActionID>(action)) {
        m_signal_action_sensitive.connect(
                [this, &widget, action] { widget.set_sensitive(get_action_sensitive(std::get<ActionID>(action))); });
        widget.set_sensitive(get_action_sensitive(std::get<ActionID>(action)));
    }
}

void Editor::handle_tool_action(const ActionConnection &conn)
{
    tool_begin(std::get<ToolID>(conn.id));
}

ActionConnection &Editor::connect_action(ToolID tool_id)
{
    return connect_action(tool_id, sigc::mem_fun(*this, &Editor::handle_tool_action));
}

ActionConnection &Editor::connect_action_with_source(ActionToolID id,
                                                     std::function<void(const ActionConnection &, ActionSource)> cb)
{
    if (m_action_connections.count(id)) {
        throw std::runtime_error("duplicate action");
    }
    if (action_catalog.count(id) == 0) {
        throw std::runtime_error("invalid action");
    }
    auto &act = m_action_connections
                        .emplace(std::piecewise_construct, std::forward_as_tuple(id), std::forward_as_tuple(id, cb))
                        .first->second;

    return act;
}

ActionConnection &Editor::connect_action(ActionToolID id, std::function<void(const ActionConnection &)> cb)
{
    return connect_action_with_source(id, [cb](const ActionConnection &conn, ActionSource) { cb(conn); });
}

std::optional<ActionToolID> Editor::get_doubleclick_action(const SelectableRef &sr)
{
    if (sr.type == SelectableRef::Type::CONSTRAINT) {
        auto &constraint = m_core.get_current_document().get_constraint(sr.item);
        if (dynamic_cast<IConstraintDatum *>(&constraint))
            return ToolID::ENTER_DATUM;
    }
    else if (sr.type == SelectableRef::Type::ENTITY) {
        auto &entity = m_core.get_current_document().get_entity(sr.item);
        switch (entity.get_type()) {
        case Entity::Type::LINE_2D:
        case Entity::Type::ARC_2D:
        case Entity::Type::BEZIER_2D:
            if (sr.point == 1 || sr.point == 2)
                return ToolID::DRAW_CONTOUR_FROM_POINT;
            break;
        case Entity::Type::WORKPLANE:
            return ActionID::ALIGN_AND_CENTER_VIEW_TO_WORKPLANE;
        case Entity::Type::TEXT:
            return ToolID::ENTER_TEXT;
        default:;
        }
    }
    else if (sr.type == SelectableRef::Type::DOCUMENT) {
        return ActionID::SET_CURRENT_DOCUMENT;
    }
    return {};
}


bool Editor::handle_action_key(Glib::RefPtr<Gtk::EventControllerKey> controller, unsigned int keyval,
                               Gdk::ModifierType state)
{
    auto ev = controller->get_current_event();
    if (ev->is_modifier())
        return false;
    state &= ~ev->get_consumed_modifiers();
    const auto original_keyval = keyval; // before mapping other keyboard layouts to Latin letters
    remap_keys(keyval, state);
    state &= (Gdk::ModifierType::SHIFT_MASK | Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::ALT_MASK);
    if (keyval == GDK_KEY_Escape) {
        if (!m_core.tool_is_active()) {
            get_canvas().set_selection_mode(SelectionMode::HOVER);
            {
                std::set<SelectableRef> sel;
                if (auto hsel = get_canvas().get_hover_selection())
                    sel.insert(*hsel);
                get_canvas().set_selection(sel, true);
            }


            reset_key_hint_label();
            if (m_keys_current.size() == 0) {
                return false;
            }
            else {
                // main_window->key_hint_set_visible(false);
                m_keys_current.clear();
                return true;
            }
        }
        else {
            ToolArgs args;
            args.type = ToolEventType::ACTION;
            args.action = InToolActionID::CANCEL;
            ToolResponse r = m_core.tool_update(args);
            tool_process(r);
            return true;
        }
    }
    else {
        return handle_typed_key(keyval, state, original_keyval);
    }
    return false;
}

/*
 * Typing in the 3D view: letters that come quickly one after another (within
 * the typing threshold, see UserConfig) are typing, not shortcuts, and go to
 * Find. A key that's a shortcut by itself waits that long before it's done;
 * keys that start a longer shortcut (d, c ...) work as before, unless what
 * follows quickly doesn't continue any shortcut.
 */
namespace {
std::string typed_char(unsigned int keyval, Gdk::ModifierType state)
{
    if ((state & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::ALT_MASK)) != Gdk::ModifierType{})
        return {};
    const auto uc = gdk_keyval_to_unicode(keyval);
    if (!uc || !g_unichar_isalnum(uc))
        return {};
    return Glib::ustring(1, uc).raw();
}
} // namespace

// what any shortcut makes of these keys: one that's complete, and/or longer ones starting with them
std::pair<bool, bool> Editor::classify_keys(const KeySequence &keys) const
{
    bool complete = false, longer = false;
    for (const auto &[id, conn] : m_action_connections) {
        for (const auto &seq : conn.key_sequences) {
            const auto m = key_sequence_match(keys, seq);
            if (m == KeyMatchResult::COMPLETE)
                complete = true;
            else if (m == KeyMatchResult::PREFIX)
                longer = true;
        }
    }
    return {complete, longer};
}

void Editor::open_find_with(std::string text) // a copy: it's often m_typed, cleared here
{
    m_pending_key.reset();
    m_pending_key_connection.disconnect();
    m_keys_current.clear();
    reset_key_hint_label();
    m_typed.clear();
    show_search(text);
}

void Editor::flush_pending_key()
{
    m_pending_key_connection.disconnect();
    if (!m_pending_key)
        return;
    const auto [keyval, state] = *m_pending_key;
    m_pending_key.reset();
    m_typed.clear();
    process_action_key(keyval, state);
}

bool Editor::handle_typed_key(unsigned int keyval, Gdk::ModifierType state, unsigned int original_keyval)
{
    const auto ch = typed_char(original_keyval, state);
    if (m_core.tool_is_active() || ch.empty()) {
        flush_pending_key(); // a waiting shortcut comes first
        m_typed.clear();
        return process_action_key(keyval, state);
    }
    const auto threshold_us = static_cast<gint64>(m_win.get_app().m_user_config.typing_threshold_ms) * 1000;
    const auto now = g_get_monotonic_time();
    const bool quick = m_typed.size() && now - m_typed_at < threshold_us;
    m_typed_at = now;

    if (quick) {
        m_typed += ch;
        if (m_pending_key) {
            // a shortcut key, followed right away by another: that's typing
            open_find_with(m_typed);
            return true;
        }
        if (m_keys_current.size()) {
            auto keys = m_keys_current;
            keys.emplace_back(keyval, state);
            const auto [complete, longer] = classify_keys(keys);
            if (complete || longer)
                return process_action_key(keyval, state);
        }
        open_find_with(m_typed);
        return true;
    }

    // a longer shortcut typed slowly
    if (m_keys_current.size()) {
        m_typed = ch;
        return process_action_key(keyval, state);
    }
    // the first key of whatever comes
    flush_pending_key();
    m_typed = ch;
    const auto [complete, longer] = classify_keys({{keyval, state}});
    if (complete && !longer) {
        // a shortcut by itself; wait whether more keys follow
        m_pending_key = {keyval, state};
        m_pending_key_connection = Glib::signal_timeout().connect(
                [this] {
                    flush_pending_key();
                    return false;
                },
                m_win.get_app().m_user_config.typing_threshold_ms);
        return true;
    }
    if (longer)
        return process_action_key(keyval, state);
    return true; // nothing by itself; maybe the start of a word
}

bool Editor::process_action_key(unsigned int keyval, Gdk::ModifierType state)
{
    m_keys_current.push_back({keyval, state});
    auto in_tool_actions = m_core.get_tool_actions();
    std::map<InToolActionID, std::pair<KeyMatchResult, KeySequence>> in_tool_actions_matched;
    std::map<ActionConnection *, std::pair<KeyMatchResult, KeySequence>> connections_matched;
    auto selection = get_canvas().get_selection();
    update_action_sensitivity();
    for (auto &[id, conn] : m_action_connections) {
        bool can_begin = false;
        if (std::holds_alternative<ToolID>(id) && !m_core.tool_is_active()) {
            can_begin = m_core.tool_can_begin(std::get<ToolID>(id), selection).get_can_begin();
        }
        else if (std::holds_alternative<ActionID>(id)) {
            can_begin = get_action_sensitive(std::get<ActionID>(id));
        }
        if (can_begin) {
            for (const auto &it2 : conn.key_sequences) {
                if (const auto m = keys_match(it2); m != KeyMatchResult::NONE) {
                    connections_matched.emplace(std::piecewise_construct, std::forward_as_tuple(&conn),
                                                std::forward_as_tuple(m, it2));
                }
            }
        }
    }
    if (in_tool_actions.size()) {
        for (const auto &[action, seqs] : m_in_tool_key_sequeces_preferences.keys) {
            if (in_tool_actions.count(action)) {
                for (const auto &seq : seqs) {
                    if (const auto m = keys_match(seq); m != KeyMatchResult::NONE)
                        in_tool_actions_matched.emplace(std::piecewise_construct, std::forward_as_tuple(action),
                                                        std::forward_as_tuple(m, seq));
                }
            }
        }
    }

    if (connections_matched.size() == 1 && in_tool_actions_matched.size() == 1) {
        m_win.set_key_hint_label_text("Ambiguous");
        m_keys_current.clear();
    }
    else if (connections_matched.size() == 1) {
        m_win.set_key_hint_label_text(key_sequence_to_string(m_keys_current));
        m_keys_current.clear();
        // main_window->key_hint_set_visible(false);
        auto conn = connections_matched.begin()->first;
        if (!trigger_action(conn->id, ActionSource::KEY)) {
            reset_key_hint_label();
            return false;
        }
        return true;
    }
    else if (in_tool_actions_matched.size() == 1) {
        m_win.set_key_hint_label_text(key_sequence_to_string(m_keys_current));
        m_keys_current.clear();

        ToolArgs args;
        args.type = ToolEventType::ACTION;
        args.action = in_tool_actions_matched.begin()->first;
        ToolResponse r = m_core.tool_update(args);
        tool_process(r);

        return true;
    }
    else if (connections_matched.size() > 1 || in_tool_actions_matched.size() > 1) { // still ambiguous
        std::list<std::pair<std::string, KeySequence>> conflicts;
        bool have_conflict = false;
        for (const auto &[conn, it] : connections_matched) {
            const auto &[res, seq] = it;
            if (res == KeyMatchResult::COMPLETE) {
                have_conflict = true;
            }
            conflicts.emplace_back(action_catalog.at(conn->id).name.full, seq);
        }
        (void)have_conflict;
        /* for (const auto &[act, it] : in_tool_actions_matched) {
             const auto &[res, seq] = it;
             if (res == KeyMatchResult::COMPLETE) {
                 have_conflict = true;
             }
             conflicts.emplace_back(in_tool_action_catalog.at(act).name + " (in-tool action)", seq);
         }
         if (have_conflict) {
             m_win.set_key_hint_label_text("Key sequences conflict");
             keys_current.clear();
             KeyConflictDialog dia(main_window, conflicts);
             if (dia.run() == KeyConflictDialog::RESPONSE_PREFS) {
                 show_preferences("keys");
             }
             return false;
         }

         for (auto ch : main_window->key_hint_box->get_children()) {
             delete ch;
         }

         for (const auto &[conn, it] : connections_matched) {
             const auto &[res, seq] = it;
             std::string seq_label;
             for (size_t i = 0; i < seq.size(); i++) {
                 seq_label += Glib::Markup::escape_text(key_sequence_item_to_string(seq.at(i))) + " ";
                 if (i + 1 == keys_current.size()) {
                     seq_label += "<b>";
                 }
             }
             rtrim(seq_label);
             seq_label += "</b>";

             auto la = Gtk::manage(new KeyLabel(main_window->key_hint_size_group, seq_label, conn->id));
             main_window->key_hint_box->append(*la);
             la->show();
         }
         main_window->key_hint_set_visible(true);
 */
        m_win.set_key_hint_label_text(key_sequence_to_string(m_keys_current) + "?");
        return true;
    }
    else if (connections_matched.size() == 0 || in_tool_actions_matched.size() == 0) {
        m_win.set_key_hint_label_text("Unknown key sequence");
        m_keys_current.clear();
        // main_window->key_hint_set_visible(false);
        return false;
    }
    else {
        Logger::log_warning("Key sequence??", Logger::Domain::EDITOR,
                            std::to_string(connections_matched.size()) + " "
                                    + std::to_string(in_tool_actions_matched.size()));
        return false;
    }
}

void Editor::on_explode_cluster(const ActionConnection &conn)
{
    auto sel = get_canvas().get_selection();
    auto &doc = m_core.get_current_document();
    auto enp = point_from_selection(doc, sel, EntityType::CLUSTER);
    if (!enp)
        return;
    auto &cluster = doc.get_entity<EntityCluster>(enp->entity);
    auto &group = doc.insert_group<GroupExplodedCluster>(UUID::random(), cluster.m_group);
    group.m_active_wrkpl = cluster.m_wrkpl;
    cluster.m_exploded_group = group.m_uuid;
    group.m_cluster = cluster.m_uuid;
    UUID content_wrkpl;
    for (const auto &[uu, en] : cluster.m_content->m_entities) {
        if (!content_wrkpl)
            content_wrkpl = dynamic_cast<const IEntityInWorkplane &>(*en).get_workplane();
        auto en_cloned = en->clone();
        en_cloned->m_group = group.m_uuid;
        dynamic_cast<IEntityInWorkplaneSet &>(*en_cloned).set_workplane(cluster.m_wrkpl);
        doc.m_entities.emplace(uu, std::move(en_cloned));
    }
    for (const auto &[uu, co] : cluster.m_content->m_constraints) {
        auto co_cloned = co->clone();
        co_cloned->m_group = group.m_uuid;
        co_cloned->replace_entity(content_wrkpl, cluster.m_wrkpl);
        doc.m_constraints.emplace(uu, std::move(co_cloned));
    }
    finish_add_group(&group);
}

void Editor::on_unexplode_cluster(const ActionConnection &conn)
{
    auto &doc = m_core.get_current_document();
    auto &group_base = doc.get_group(m_core.get_current_group());
    if (group_base.get_type() != Group::Type::EXPLODED_CLUSTER)
        return;
    auto &group = dynamic_cast<GroupExplodedCluster &>(group_base);
    if (!group.m_active_wrkpl)
        return;
    CanvasUpdater canvas_updater{*this};

    auto &cluster = doc.get_entity<EntityCluster &>(group.m_cluster);

    auto content = ClusterContent::create();

    auto cloned_wrkpl_uu = doc.get_reference_group().get_workplane_xy_uuid();

    for (auto &[uu, en] : doc.m_entities) {
        if (en->m_group != group.m_uuid)
            continue;
        if (dynamic_cast<const IEntityInWorkplane &>(*en).get_workplane() != group.m_active_wrkpl)
            continue;
        auto en_cloned = en->clone();
        en_cloned->m_group = cluster.m_group;
        dynamic_cast<IEntityInWorkplaneSet &>(*en_cloned).set_workplane(cloned_wrkpl_uu);
        content->m_entities.emplace(uu, std::move(en_cloned));
    }
    for (auto &[uu, co] : doc.m_constraints) {
        if (co->m_group != group.m_uuid)
            continue;
        auto co_cloned = co->clone();
        co_cloned->replace_entity(group.m_active_wrkpl, cloned_wrkpl_uu);
        co_cloned->m_group = cluster.m_group;
        content->m_constraints.emplace(uu, std::move(co_cloned));
    }

    cluster.m_content = content;

    ItemsToDelete items_to_delete;
    items_to_delete.groups.insert(group.m_uuid);
    auto extra_items = doc.get_additional_items_to_delete(items_to_delete);
    items_to_delete.append(extra_items);
    cluster.m_exploded_group = UUID{};

    std::set<EntityAndPoint> deleted_anchors_enps;
    for (const auto &[a, enp] : cluster.m_anchors) {
        bool present = false;
        if (content->m_entities.count(enp.entity)) {
            auto &en = content->m_entities.at(enp.entity);
            present = en->is_valid_point(enp.point);
        }

        if (!present)
            deleted_anchors_enps.emplace(cluster.m_uuid, a);
    }

    for (auto a : deleted_anchors_enps) {
        cluster.remove_anchor(a.point);
    }

    for (const auto &[uu, constr] : doc.m_constraints) {
        std::set<EntityAndPoint> isect;
        std::ranges::set_intersection(constr->get_referenced_entities_and_points(), deleted_anchors_enps,
                                      std::inserter(isect, isect.begin()));
        if (isect.size())
            items_to_delete.constraints.insert(uu);
    }


    doc.set_group_generate_pending(cluster.m_group);

    doc.delete_items(items_to_delete);

    m_core.set_needs_save();
    m_core.rebuild("unexplode cluster");
    m_workspace_browser->update_documents(get_current_document_views());
    m_workspace_browser->select_group(cluster.m_group);
}


void Editor::on_view_rotate(const ActionConnection &conn)
{
    const auto action = std::get<ActionID>(conn.id);

    glm::vec3 direction = {0, 0, 0};
    if (action == ActionID::VIEW_ROTATE_UP)
        direction = glm::vec3(1, 0, 0);
    else if (action == ActionID::VIEW_ROTATE_DOWN)
        direction = glm::vec3(-1, 0, 0);
    else if (action == ActionID::VIEW_ROTATE_LEFT)
        direction = glm::vec3(0, 1, 0);
    else if (action == ActionID::VIEW_ROTATE_RIGHT)
        direction = glm::vec3(0, -1, 0);
    else if (action == ActionID::VIEW_TILT_LEFT)
        direction = glm::vec3(0, 0, -1);
    else if (action == ActionID::VIEW_TILT_RIGHT)
        direction = glm::vec3(0, 0, 1);

    auto r = glm::angleAxis(glm::radians(90.f), direction);
    get_canvas().animate_to_cam_quat_rel(r);
}

void Editor::on_view_zoom(const ActionConnection &conn)
{
    const auto action = std::get<ActionID>(conn.id);

    auto factor = 1;
    if (action == ActionID::VIEW_ZOOM_IN)
        factor = -1;
    else if (action == ActionID::VIEW_ZOOM_OUT)
        factor = 1;

    get_canvas().animate_zoom(factor, Canvas::ZoomCenter::SCREEN);
}

void Editor::on_view_pan(const ActionConnection &conn)
{
    const auto action = std::get<ActionID>(conn.id);

    glm::vec2 direction = {0, 0};
    if (action == ActionID::VIEW_PAN_UP)
        direction = glm::vec2(0, 1);
    else if (action == ActionID::VIEW_PAN_DOWN)
        direction = glm::vec2(0, -1);
    else if (action == ActionID::VIEW_PAN_LEFT)
        direction = glm::vec2(-1, 0);
    else if (action == ActionID::VIEW_PAN_RIGHT)
        direction = glm::vec2(1, 0);

    get_canvas().animate_pan(direction * 50.f);
}

void Editor::on_view_set(const ActionConnection &conn)
{
    const auto action = std::get<ActionID>(conn.id);

    glm::quat q;

    switch (action) {
    case ActionID::VIEW_FRONT:
        q = glm::quatLookAt(glm::vec3(0, 1, 0), glm::vec3(0, 0, 1));
        break;
    case ActionID::VIEW_ISO:
        q = glm::quatLookAt(glm::normalize(glm::vec3(-1, 1.3, -.9)), glm::vec3(0, 0, 1));
        break;
    case ActionID::VIEW_BACK:
        q = glm::quatLookAt(glm::vec3(0, -1, 0), glm::vec3(0, 0, 1));
        break;
    case ActionID::VIEW_TOP:
        q = glm::quatLookAt(glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
        break;
    case ActionID::VIEW_BOTTOM:
        q = glm::quatLookAt(glm::vec3(0, 0, 1), glm::vec3(0, -1, 0));
        break;
    case ActionID::VIEW_RIGHT:
        q = glm::quatLookAt(glm::vec3(-1, 0, 0), glm::vec3(0, 0, 1));
        break;
    case ActionID::VIEW_LEFT:
        q = glm::quatLookAt(glm::vec3(1, 0, 0), glm::vec3(0, 0, 1));
        break;
    default:
        return;
    }


    auto sq = get_canvas().get_tilt_snapped_quat(q);

    get_canvas().animate_to_cam_quat(sq);
}


} // namespace dune3d

#pragma once
#include <gtkmm.h>
#include <functional>
#include "util/uuid.hpp"
#include "document/group/group.hpp"
#include "document/document.hpp"
#include "document/object_folders.hpp"

namespace dune3d {

class Core;
class DocumentView;
class IDocumentInfo;
enum class WorkspaceBrowserAddGroupMode { WITH_BODY, WITHOUT_BODY };

class WorkspaceBrowser : public Gtk::Box {
public:
    WorkspaceBrowser(Core &core);

    void update_documents(const std::map<UUID, DocumentView> &doc_views);
    void update_current_group(const std::map<UUID, DocumentView> &doc_views);
    void update_needs_save();
    // off: only the current document and the ones shown in the 3D view are listed
    void set_show_other_documents(bool show)
    {
        m_show_other_documents = show;
    }
    // on: just the current document is listed, without a checkbox
    void set_one_document_per_screen(bool one)
    {
        m_one_document_per_screen = one;
    }
    // on: no document rows, the objects of the current document are the top of the list
    void set_simple(bool simple)
    {
        m_simple = simple;
    }
    // off: objects only, no step rows under them (and no base "Body" with just the reference plane)
    void set_show_steps(bool show)
    {
        m_show_steps = show;
    }
    // the screens (tabs) a document is shown in, for its row; empty = nothing to show
    std::function<std::string(const UUID &)> m_screens_text;


    using type_signal_group_selected = sigc::signal<void(UUID, UUID)>;
    using type_signal_move_body = sigc::signal<void(UUID, UUID, int)>;
    type_signal_group_selected signal_group_selected()
    {
        return m_signal_group_selected;
    }

    // a click on the step that is already the current one
    type_signal_group_selected signal_group_reclicked()
    {
        return m_signal_group_reclicked;
    }

    type_signal_group_selected signal_rename_body()
    {
        return m_signal_rename_body;
    }

    // selects the n-th visible row (body or group) with that name, as a click would
    bool select_row_by_name(const std::string &name, unsigned int nth = 0);
    bool select_body(const UUID &doc_uu, const UUID &body_uu);

    type_signal_group_selected signal_body_selected()
    {
        return m_signal_body_selected;
    }

    // a row of a group of objects (see document/object_folders.hpp): selected / eye switched
    using type_signal_folder_selected = sigc::signal<void(UUID, std::string)>;
    using type_signal_folder_checked = sigc::signal<void(UUID, std::string, bool)>;
    type_signal_folder_selected signal_folder_selected()
    {
        return m_signal_folder_selected;
    }
    type_signal_folder_checked signal_folder_checked()
    {
        return m_signal_folder_checked;
    }

    // organising objects in groups: drops in the list, renaming and ungrouping a group, putting one object in a
    // group (a new one if the name is empty and signal_new_folder is used), taking it out
    using type_signal_folder_drop = sigc::signal<void(UUID, FolderDrop)>;
    using type_signal_folder_rename = sigc::signal<void(UUID, std::string, std::string)>;
    using type_signal_folder_name = sigc::signal<void(UUID, std::string)>;
    using type_signal_object_folder = sigc::signal<void(UUID, UUID, std::string)>;
    type_signal_folder_drop signal_folder_drop()
    {
        return m_signal_folder_drop;
    }
    type_signal_folder_rename signal_folder_rename()
    {
        return m_signal_folder_rename;
    }
    type_signal_folder_name signal_ungroup_folder()
    {
        return m_signal_ungroup_folder;
    }
    type_signal_group_selected signal_new_folder()
    {
        return m_signal_new_folder;
    }
    type_signal_object_folder signal_object_folder()
    {
        return m_signal_object_folder;
    }

    type_signal_group_selected signal_delete_body()
    {
        return m_signal_delete_body;
    }

    type_signal_group_selected signal_set_body_color()
    {
        return m_signal_set_body_color;
    }

    type_signal_group_selected signal_reset_body_color()
    {
        return m_signal_reset_body_color;
    }

    type_signal_move_body signal_move_body()
    {
        return m_signal_move_body;
    }

    using type_signal_group_checked = sigc::signal<void(UUID, UUID, bool)>;
    using type_signal_document_checked = sigc::signal<void(UUID, bool)>;
    type_signal_group_checked signal_group_checked()
    {
        return m_signal_group_checked;
    }

    type_signal_group_checked signal_body_checked()
    {
        return m_signal_body_checked;
    }

    type_signal_group_checked signal_body_solid_model_checked()
    {
        return m_signal_body_solid_model_checked;
    }

    type_signal_document_checked signal_document_checked()
    {
        return m_signal_document_checked;
    }

    using type_signal_delete_current_group = sigc::signal<void()>;

    using AddGroupMode = WorkspaceBrowserAddGroupMode;
    using type_signal_add_group = sigc::signal<void(Group::Type, AddGroupMode)>;

    type_signal_delete_current_group signal_delete_current_group()
    {
        return m_signal_delete_current_group;
    }

    type_signal_add_group signal_add_group()
    {
        return m_signal_add_group;
    }

    using type_signal_move_group = sigc::signal<void(Document::MoveGroup)>;
    type_signal_move_group signal_move_group()
    {
        return m_signal_move_group;
    }

    using type_signal_close_document = sigc::signal<void(UUID)>;
    type_signal_close_document signal_close_document()
    {
        return m_signal_close_document;
    }

    using type_signal_activate_link = sigc::signal<void(std::string)>;
    type_signal_activate_link signale_active_link()
    {
        return m_signal_activate_link;
    }

    using type_signal_item_expanded = sigc::signal<bool(UUID, bool)>;
    type_signal_item_expanded signal_body_expanded()
    {
        return m_signal_body_expanded;
    }


    void group_prev_next(int dir);
    void select_group(const UUID &uu);

    void show_toast(const std::string &msg);

private:
    Gtk::ListView *m_view = nullptr;


    class DocumentItem;
    class BodyItem;
    class GroupItem;
    class WorkspaceRow;
    friend class WorkspaceRow;
    Glib::RefPtr<Gio::ListStore<DocumentItem>> m_document_store;

    Glib::RefPtr<Gio::ListModel> create_model(const Glib::RefPtr<Glib::ObjectBase> &item = {});
    Glib::RefPtr<Gtk::TreeListModel> m_model;
    Glib::RefPtr<Gtk::SingleSelection> m_selection_model;
    Gtk::Revealer *m_toast_revealer = nullptr;
    Gtk::Label *m_toast_label = nullptr;
    Gtk::InfoBar *m_info_bar = nullptr;
    Gtk::Image *m_info_bar_icon = nullptr;
    Gtk::Label *m_info_bar_label = nullptr;

    Core &m_core;

    type_signal_group_selected m_signal_group_selected;
    type_signal_group_selected m_signal_group_reclicked;
    type_signal_folder_selected m_signal_folder_selected;
    type_signal_folder_checked m_signal_folder_checked;
    type_signal_folder_drop m_signal_folder_drop;
    type_signal_folder_rename m_signal_folder_rename;
    type_signal_folder_name m_signal_ungroup_folder;
    type_signal_group_selected m_signal_new_folder;
    type_signal_object_folder m_signal_object_folder;
    type_signal_group_checked m_signal_group_checked;
    type_signal_group_checked m_signal_body_checked;
    type_signal_group_checked m_signal_body_solid_model_checked;
    type_signal_document_checked m_signal_document_checked;

    type_signal_delete_current_group m_signal_delete_current_group;
    type_signal_add_group m_signal_add_group;
    type_signal_move_group m_signal_move_group;
    type_signal_close_document m_signal_close_document;
    type_signal_activate_link m_signal_activate_link;

    type_signal_group_selected m_signal_rename_body;
    type_signal_group_selected m_signal_body_selected;
    type_signal_group_selected m_signal_delete_body;
    type_signal_group_selected m_signal_set_body_color;
    type_signal_group_selected m_signal_reset_body_color;
    type_signal_move_body m_signal_move_body;

    type_signal_item_expanded m_signal_body_expanded;

    void emit_add_group(GroupType type, AddGroupMode add_group_mode = AddGroupMode::WITHOUT_BODY);
    bool emit_body_expanded(const UUID &body_uu, bool expanded);

    void block_signals();
    void unblock_signals();
    unsigned int m_blocked_count = 0;
    bool m_show_other_documents = false;
    bool m_one_document_per_screen = false;
    bool m_simple = false;
    bool m_show_steps = true;

    void select_group(const UUID &doc_uu, const UUID &uu);


    static void update_name(DocumentItem &it, IDocumentInfo &doci);

    sigc::connection m_toast_connection;

    Gtk::PopoverMenu *m_body_popover = nullptr;
    Glib::RefPtr<Gio::Menu> m_body_menu = nullptr;
    UUID m_body_menu_document;
    UUID m_body_menu_body;
    std::string m_body_menu_folder; // the group the object the menu is for is in
    Glib::RefPtr<Gio::SimpleAction> m_reset_body_color_action;
    Glib::RefPtr<Gio::SimpleAction> m_move_body_up_action;
    Glib::RefPtr<Gio::SimpleAction> m_move_body_down_action;
    // the object menu's group entries are made again each time it opens
    Glib::RefPtr<Gio::Menu> m_body_groups_menu;
    Glib::RefPtr<Gio::SimpleAction> m_remove_from_group_action;
    // a group's row menu
    Gtk::PopoverMenu *m_folder_popover = nullptr;
    UUID m_folder_menu_document;
    std::string m_folder_menu_name;
    std::function<void()> m_folder_menu_rename; // opens the rename box of the row the menu is for

    // an object or group dropped below the last row: it goes to the end of the list, out of its group
    void drop_at_end(const std::string &payload);
};
} // namespace dune3d

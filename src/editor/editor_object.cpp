#include "editor.hpp"
#include "document/object_parts.hpp"
#include "document/object_folders.hpp"
#include "document/object_dimensions.hpp"
#include "group_editor/group_editor.hpp"
#include "canvas/color_palette.hpp"
#include "preferences/preferences.hpp"
#include "dune3d_appwindow.hpp"
#include "workspace_browser.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/group/group_extrude.hpp"
#include "document/constraint/constraint.hpp"
#include "document/constraint/iconstraint_datum.hpp"
#include "document/constraint/constraint_point_distance.hpp"
#include "document/entity/entity.hpp"
#include "widgets/spin_button_dim.hpp"
#include "util/gtk_util.hpp"
#include "panel_hints.hpp"
#include "util/util.hpp"
#include "document/face_sketch.hpp"
#include "document/body_solids.hpp"
#include "document/combine.hpp"
#include "document/pattern.hpp"
#include "document/decal.hpp"
#include "document/duplicate.hpp"
#include "document/round_edges.hpp"
#include "document/hollow.hpp"
#include "document/entity/entity_text.hpp"
#include "util/text_render.hpp"
#include "document/group/group_fillet.hpp"
#include "document/primitives.hpp"
#include "document/material.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include "canvas/canvas.hpp"
#include <ranges>

namespace dune3d {

/*
 * Object panel: shown in place of the group editor when a body is picked in
 * the workspace browser. Covers what one usually wants to change about a
 * whole object - name, colour, visibility, its dimensions - and deleting it.
 */

namespace {
// the ready-made shapes: placed by X/Y distances from the workplane origin
bool has_xy_position(const Document &doc, const UUID &body)
{
    return std::ranges::any_of(get_object_dimensions(doc, body), [](const auto &d) { return d.label == "X"; });
}
} // namespace

bool Editor::go_to_last_group(const UUID &doc_uu)
{
    if (!m_core.has_documents() || m_core.get_current_idocument_info().get_uuid() != doc_uu)
        return false;
    const auto last = m_core.get_current_document().get_groups_sorted().back()->m_uuid;
    if (m_core.get_current_group() == last)
        return false;
    get_current_document_view().m_current_group = last;
    set_current_group(last);
    return true;
}

// The panel is built as one grid with a heading row for each section; this splits it at the headings
// into cards, a grid each (the heading is its first row), so each section stands on its own.
static Gtk::Box *make_cards(Gtk::Grid &grid)
{
    struct Item {
        Gtk::Widget *w;
        int col, row, cw, rh;
    };
    std::vector<Item> items;
    for (auto ch = grid.get_first_child(); ch; ch = ch->get_next_sibling()) {
        Item it{ch, 0, 0, 1, 1};
        gtk_grid_query_child(grid.gobj(), ch->gobj(), &it.col, &it.row, &it.cw, &it.rh);
        items.push_back(it);
    }
    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.row < b.row; });
    auto outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    outer->add_css_class("object-editor");
    outer->add_css_class("object-cards");
    Gtk::Grid *card = nullptr;
    int base = 0;
    for (const auto &it : items) {
        auto la = dynamic_cast<Gtk::Label *>(it.w);
        const bool heading = la && la->has_css_class("object-editor-heading");
        if (heading || !card) {
            card = Gtk::make_managed<Gtk::Grid>();
            card->set_row_spacing(8);
            card->set_column_spacing(10);
            card->add_css_class("object-card");
            outer->append(*card);
            base = it.row;
        }
        g_object_ref(it.w->gobj()); // survives being taken out of the old grid
        grid.remove(*it.w);
        card->attach(*it.w, it.col, it.row - base, it.cw, it.rh);
        g_object_unref(it.w->gobj());
    }
    return outer;
}

std::vector<Gtk::Grid *> Editor::get_object_editor_grids() const
{
    std::vector<Gtk::Grid *> grids;
    if (auto grid = dynamic_cast<Gtk::Grid *>(m_object_editor))
        grids.push_back(grid);
    else if (m_object_editor) {
        for (auto ch = m_object_editor->get_first_child(); ch; ch = ch->get_next_sibling()) {
            if (auto g = dynamic_cast<Gtk::Grid *>(ch))
                grids.push_back(g);
        }
    }
    return grids;
}

void Editor::show_object_editor(const UUID &doc_uu, const UUID &body_uu)
{
    if (m_group_editor) {
        if (m_delayed_commit_connection.connected())
            commit_from_editor();
        remove_from_side_panel(*m_group_editor);
        m_group_editor = nullptr;
    }
    // the panel is rebuilt after every change: keep the focus on the same field
    std::string focus_label, focus_name;
    double scroll = 0; // and where the panel was scrolled to
    if (m_object_editor && m_object_editor_doc == doc_uu && m_object_editor_body == body_uu) {
        scroll = m_side_panel_scrolled->get_vadjustment()->get_value();
        {
            for (auto focus = m_win.get_focus(); focus && !focus_label.size(); focus = focus->get_parent()) {
                auto grid = dynamic_cast<Gtk::Grid *>(focus->get_parent());
                if (!grid || grid->get_parent() != m_object_editor)
                    continue;
                // fields of parts are named: their labels repeat
                if (focus->get_name().size() && focus->get_name() != G_OBJECT_TYPE_NAME(focus->gobj()))
                    focus_name = focus->get_name();
                int col, row, w, h;
                gtk_grid_query_child(grid->gobj(), focus->gobj(), &col, &row, &w, &h);
                if (auto la = dynamic_cast<Gtk::Label *>(grid->get_child_at(col - 1, row)))
                    focus_label = la->get_text();
            }
        }
    }
    if (m_object_editor) {
        remove_from_side_panel(*m_object_editor);
        m_object_editor = nullptr;
    }
    auto &doc = m_core.get_idocument_info(doc_uu).get_document();
    // the browser may still report a row that was just deleted
    if (!doc.get_groups().contains(body_uu))
        return;
    auto &body_group = doc.get_group(body_uu);
    if (!body_group.m_body)
        return;
    m_object_editor_doc = doc_uu;
    if (m_object_editor_body != body_uu)
        m_object_editor_part = UUID();
    m_object_editor_body = body_uu;
    m_object_combine_dropdown = nullptr;

    auto grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(8);
    grid->set_column_spacing(10);
    grid->set_margin(14);
    grid->add_css_class("object-editor");
    m_object_editor = grid;
    int top = 0;

    auto heading = [grid, &top](const std::string &text) {
        auto la = Gtk::make_managed<Gtk::Label>(text);
        la->set_xalign(0);
        la->add_css_class("object-editor-heading");
        grid->attach(*la, 0, top++, 2, 1);
    };

    auto changed = [this, doc_uu](const UUID &group, bool solve) {
        auto &d = m_core.get_idocument_info(doc_uu).get_document();
        if (solve)
            d.set_group_solve_pending(group);
        else
            d.set_group_update_solid_model_pending(group);
        m_core.set_needs_save();
        m_object_editor_keep = true; // the panel already shows the new value
        m_core.rebuild("edit object");
        canvas_update_keep_selection();
        m_workspace_browser->update_documents(get_current_document_views());
    };

    heading("Object");
    {
        auto en = Gtk::make_managed<Gtk::Entry>();
        en->set_text(body_group.m_body->m_name);
        en->set_hexpand(true);
        en->signal_activate().connect([this, en, doc_uu, body_uu, changed] {
            auto &d = m_core.get_idocument_info(doc_uu).get_document();
            d.get_group(body_uu).m_body->m_name = en->get_text();
            changed(body_uu, false);
        });
        grid_attach_label_and_widget(*grid, "Name", *en, top);
    }
    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
        auto dia = Gtk::ColorDialog::create();
        dia->set_with_alpha(false);
        auto bu = Gtk::make_managed<Gtk::ColorDialogButton>(dia);
        bu->set_hexpand(true);
        Color initial = body_group.m_body->m_color.value_or(Preferences::get().canvas.appearance.get_color(
                ColorP::SOLID_MODEL));
        bu->set_rgba(rgba_from_color(initial));
        bu->property_rgba().signal_changed().connect([this, bu, doc_uu, body_uu, changed] {
            auto &d = m_core.get_idocument_info(doc_uu).get_document();
            d.get_group(body_uu).m_body->m_color = color_from_rgba(bu->get_rgba());
            changed(body_uu, false);
        });
        box->append(*bu);
        auto reset = Gtk::make_managed<Gtk::Button>("Default");
        reset->set_tooltip_text("Use the default colour");
        reset->signal_clicked().connect([this, doc_uu, body_uu, changed] {
            auto &d = m_core.get_idocument_info(doc_uu).get_document();
            d.get_group(body_uu).m_body->m_color.reset();
            changed(body_uu, false);
            show_object_editor(doc_uu, body_uu);
        });
        box->append(*reset);
        grid_attach_label_and_widget(*grid, "Colour", *box, top);
    }
    {
        guint selected = 0;
        auto items = Gtk::StringList::create();
        const auto &materials = get_all_materials();
        for (size_t i = 0; i < materials.size(); i++) {
            items->append(get_material_label(materials.at(i)));
            if (materials.at(i) == body_group.m_body->m_material)
                selected = i;
        }
        auto dd = Gtk::make_managed<Gtk::DropDown>(items);
        dd->set_selected(selected);
        dd->property_selected().signal_changed().connect([this, dd, doc_uu, body_uu, changed] {
            auto &d = m_core.get_idocument_info(doc_uu).get_document();
            d.get_group(body_uu).m_body->m_material = get_all_materials().at(dd->get_selected());
            changed(body_uu, false);
        });
        grid_attach_label_and_widget(*grid, "Material", *dd, top);
    }
    {
        // like Tinkercad's holes: see-through, not printed, and "Cut" removes it from what it overlaps
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto sw = Gtk::make_managed<Gtk::Switch>();
        sw->set_valign(Gtk::Align::CENTER);
        sw->set_active(body_group.m_body->m_hole);
        sw->set_tooltip_text("A hole is drawn see-through and isn't exported; use Cut to remove it from the "
                             "objects it overlaps");
        sw->property_active().signal_changed().connect([this, sw, doc_uu, body_uu, changed] {
            auto &d = m_core.get_idocument_info(doc_uu).get_document();
            d.get_group(body_uu).m_body->m_hole = sw->get_active();
            changed(body_uu, false);
            Glib::signal_idle().connect_once([this, doc_uu, body_uu] { show_object_editor(doc_uu, body_uu); });
        });
        box->append(*sw);
        if (body_group.m_body->m_hole) {
            auto cut = Gtk::make_managed<Gtk::Button>("Cut");
            cut->add_css_class("cut-action");
            cut->set_tooltip_text("Remove this hole from every shown object it overlaps");
            cut->signal_clicked().connect([this, doc_uu, body_uu] {
                Glib::signal_idle().connect_once([this, doc_uu, body_uu] { cut_with_hole(doc_uu, body_uu); });
            });
            box->append(*cut);
        }
        grid_attach_label_and_widget(*grid, "Hole", *box, top);
    }
    {
        auto sw = Gtk::make_managed<Gtk::Switch>();
        sw->set_halign(Gtk::Align::START);
        sw->set_active(get_current_document_views()[doc_uu].m_body_views[body_uu].m_visible);
        sw->property_active().signal_changed().connect([this, sw, doc_uu, body_uu] {
            CanvasUpdater canvas_updater{*this};
            get_current_document_views()[doc_uu].m_body_views[body_uu].m_visible = sw->get_active();
            m_workspace_browser->update_current_group(get_current_document_views());
        });
        grid_attach_label_and_widget(*grid, "Visible", *sw, top);
    }

    // a group of objects: listed together and moved together, nothing is joined
    {
        auto en = Gtk::make_managed<Gtk::Entry>();
        en->set_text(body_group.m_body->m_folder);
        en->set_placeholder_text("none");
        en->set_hexpand(true);
        en->set_tooltip_text("Name of a group of objects. Objects with the same name are listed together and "
                             "move together, but stay separate shapes. Empty: not in a group.");
        en->signal_activate().connect([this, en, doc_uu, body_uu, changed] {
            auto &d = m_core.get_idocument_info(doc_uu).get_document();
            const std::string name = en->get_text().raw();
            if (d.get_group(body_uu).m_body->m_folder == name)
                return;
            set_body_folder(d, body_uu, name);
            changed(body_uu, false);
            Glib::signal_idle().connect_once([this, doc_uu, body_uu] { show_object_editor(doc_uu, body_uu); });
        });
        grid_attach_label_and_widget(*grid, "Group", *en, top);
        if (const auto &folder = body_group.m_body->m_folder; folder.size()) {
            auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
            auto ren = Gtk::make_managed<Gtk::Entry>();
            ren->set_text(folder);
            ren->set_hexpand(true);
            ren->set_tooltip_text("Rename the group (for all its objects)");
            ren->signal_activate().connect([this, ren, doc_uu, body_uu, folder, changed] {
                auto &d = m_core.get_idocument_info(doc_uu).get_document();
                const std::string name = ren->get_text().raw();
                if (name.empty() || name == folder)
                    return;
                rename_folder(d, folder, name);
                changed(body_uu, false);
                Glib::signal_idle().connect_once([this, doc_uu, body_uu] { show_object_editor(doc_uu, body_uu); });
            });
            box->append(*ren);
            auto ungroup = Gtk::make_managed<Gtk::Button>("Ungroup");
            ungroup->set_tooltip_text("Take every object out of this group (they stay where they are)");
            ungroup->signal_clicked().connect([this, doc_uu, body_uu, folder, changed] {
                auto &d = m_core.get_idocument_info(doc_uu).get_document();
                rename_folder(d, folder, "");
                changed(body_uu, false);
                Glib::signal_idle().connect_once([this, doc_uu, body_uu] { show_object_editor(doc_uu, body_uu); });
            });
            box->append(*ungroup);
            grid_attach_label_and_widget(*grid, "Rename group", *box, top);
        }
    }

    // 3D text: what it says and how tall it is
    if (auto text = find_text_entity(doc, body_uu)) {
        heading("Text");
        auto en = Gtk::make_managed<Gtk::Entry>();
        en->set_text(text->m_text);
        en->set_hexpand(true);
        const UUID text_uu = text->m_uuid;
        en->signal_activate().connect([this, en, doc_uu, text_uu] {
            auto &d = m_core.get_idocument_info(doc_uu).get_document();
            auto &t = d.get_entity<EntityText>(text_uu);
            if (en->get_text().empty() || t.m_text == en->get_text().raw())
                return;
            t.m_text = en->get_text();
            render_text(t, get_pango_context(), d);
            d.set_group_generate_pending(t.m_group);
            m_core.set_needs_save();
            m_core.rebuild("edit text");
            canvas_update_keep_selection();
        });
        grid_attach_label_and_widget(*grid, "Says", *en, top);
        auto sp = Gtk::make_managed<SpinButtonDim>();
        sp->set_range(0.1, 10000);
        sp->set_hexpand(true);
        {
            const auto [lo, hi] = text->get_bbox();
            sp->set_value(hi.y - lo.y);
        }
        sp->signal_value_changed().connect([this, sp, doc_uu, text_uu] {
            auto &d = m_core.get_idocument_info(doc_uu).get_document();
            auto &t = d.get_entity<EntityText>(text_uu);
            const auto [lo, hi] = t.get_bbox();
            if (hi.y <= lo.y || sp->get_value() <= 0 || std::abs(sp->get_value() - (hi.y - lo.y)) < 1e-9)
                return;
            t.m_scale *= sp->get_value() / (hi.y - lo.y);
            d.set_group_generate_pending(t.m_group);
            m_core.set_needs_save();
            m_object_editor_keep = true;
            m_core.rebuild("text size");
            canvas_update_keep_selection();
        });
        grid_attach_label_and_widget(*grid, "Size", *sp, top).add_css_class("tnum");
    }

    // every dimension in the object's groups, editable in place
    const auto body_groups = doc.get_groups_by_body();
    auto it = std::ranges::find_if(body_groups, [&body_uu](const auto &bg) { return bg.get_group().m_uuid == body_uu; });
    std::vector<const Group *> groups;
    if (it != body_groups.end())
        groups = it->groups;
    const auto dims = get_object_dimensions(doc, body_uu);
    if (dims.size())
        heading("Dimensions");
    for (const auto &dim : dims) {
        auto datum = dynamic_cast<IConstraintDatum *>(doc.m_constraints.at(dim.constraint).get());
        auto sp = &make_dimension_spin(*datum, dim);
        const UUID co_uu = dim.constraint;
        const UUID group_uu = dim.group;
        const double scale = dim.scale;
        sp->signal_value_changed().connect([this, sp, doc_uu, co_uu, group_uu, scale, changed] {
            auto &d = m_core.get_idocument_info(doc_uu).get_document();
            auto &c = dynamic_cast<IConstraintDatum &>(*d.m_constraints.at(co_uu));
            if (c.get_datum() == sp->get_value() / scale)
                return;
            c.set_datum(sp->get_value() / scale);
            changed(group_uu, true);
        });
        grid_attach_label_and_widget(*grid, dim.label, *sp, top).add_css_class("tnum");
        // a size can also be slid: from tiny to well past what it is now (the field takes any value)
        if (sp->get_value() > 0 && dim.label != "X" && dim.label != "Y" && dim.label != "Z") {
            const double v = sp->get_value();
            const double hi = std::max(100., std::ceil(v * 2.5 / 10) * 10);
            auto slider = Gtk::make_managed<Gtk::Scale>(Gtk::Orientation::HORIZONTAL);
            slider->set_range(1, hi);
            slider->set_value(std::clamp(v, 1., hi));
            slider->set_draw_value(false);
            slider->set_round_digits(0);
            slider->set_hexpand(true);
            slider->add_css_class("dim-slider");
            slider->set_tooltip_text("Slide to change the " + dim.label);
            auto syncing = std::make_shared<bool>(false);
            slider->signal_value_changed().connect([sp, slider, syncing] {
                if (*syncing)
                    return;
                *syncing = true;
                sp->set_value(slider->get_value());
                *syncing = false;
            });
            sp->signal_value_changed().connect([sp, slider, syncing, hi] {
                if (*syncing)
                    return;
                *syncing = true;
                slider->set_value(std::clamp(sp->get_value(), 1., hi));
                *syncing = false;
            });
            grid->attach(*slider, 0, top++, 2, 1);
        }
    }

    // rounding all edges, like a rounded box
    if (std::ranges::any_of(get_body_solids(doc, UUID()), [&body_uu](const auto &bs) { return bs.body == body_uu; })) {
        auto sp = Gtk::make_managed<SpinButtonDim>();
        // with walls, it can't go past what they allow
        sp->set_range(0, std::min(1000., get_max_round_edges(doc, body_uu)));
        sp->set_hexpand(true);
        auto fillet = find_round_edges(doc, body_uu);
        sp->set_value(fillet ? fillet->m_radius : 0);
        sp->set_tooltip_text("Rounds all edges; 0 for sharp edges");
        auto status = Gtk::make_managed<Gtk::Label>();
        status->add_css_class("dim-label");
        status->set_xalign(0);
        status->set_wrap(true);
        if (fillet && !fillet->get_solid_model())
            status->set_text(find_hollow(doc, body_uu) ? "Too round for the walls - at most about half their thickness"
                                                       : "That radius is too big for this object");
        sp->signal_value_changed().connect([this, sp, doc_uu, body_uu] {
            auto &d = m_core.get_idocument_info(doc_uu).get_document();
            const auto r = sp->get_value();
            const auto existing = find_round_edges(d, body_uu);
            const double was = existing ? existing->m_radius : 0;
            if (was == r)
                return;
            // too round for it: back to what worked, rather than a broken object
            if (!try_round_edges(d, body_uu, r, was)) {
                if (!find_round_edges(d, body_uu) && was == 0)
                    m_workspace_browser->show_toast("This object has no edges to round");
                else
                    m_workspace_browser->show_toast("That's as round as this object gets");
                sp->set_value(was);
                return;
            }
            auto rounded = find_round_edges(d, body_uu);
            m_core.set_needs_save();
            // the panel is made again once the clicks are over, see schedule_object_panel_refresh
            m_object_editor_keep = true;
            m_core.rebuild("round edges");
            canvas_update_keep_selection();
            m_workspace_browser->update_documents(get_current_document_views());
            // the view only shows groups up to the current one: move past the new fillet so the
            // rounding is visible
            const auto current = m_core.get_current_idocument_info().get_uuid() == doc_uu
                                         ? m_core.get_current_group()
                                         : get_current_document_views()[doc_uu].m_current_group;
            const bool go_to_last = rounded && d.get_groups().contains(current)
                                    && rounded->get_index() > d.get_group(current).get_index();
            schedule_object_panel_refresh(go_to_last ? 300 : 700, go_to_last);
        });
        if (dims.empty())
            heading("Dimensions");
        grid_attach_label_and_widget(*grid, "Round edges", *sp, top).add_css_class("tnum");
        if (status->get_text().size())
            grid->attach(*status, 1, top++, 1, 1);
    }

    // hollowing out, like a cup or a box without its lid
    if (std::ranges::any_of(get_body_solids(doc, UUID()), [&body_uu](const auto &bs) { return bs.body == body_uu; })) {
        auto hollow = find_hollow(doc, body_uu);
        // two rows: side by side they'd make the panel wider than the sidebar
        auto sp = Gtk::make_managed<SpinButtonDim>();
        sp->set_range(0, 1000);
        sp->set_increments(.5, 1);
        sp->set_hexpand(true);
        sp->set_value(hollow ? hollow->m_thickness : 0);
        sp->set_tooltip_text("Wall thickness; 0 for a solid object");
        auto items = Gtk::StringList::create({"Open top", "Open bottom", "Closed"});
        auto dd = Gtk::make_managed<Gtk::DropDown>(items);
        dd->set_selected(static_cast<guint>(hollow ? hollow->m_open : GroupShell::Open::TOP));
        dd->set_tooltip_text("Which side stays open");
        auto apply = [this, sp, dd, doc_uu, body_uu] {
            auto &d = m_core.get_idocument_info(doc_uu).get_document();
            const auto t = sp->get_value();
            const auto open = static_cast<GroupShell::Open>(dd->get_selected());
            const auto existing = find_hollow(d, body_uu);
            const double was = existing ? existing->m_thickness : 0;
            if (was == t && (!existing || existing->m_open == open))
                return;
            // walls too thick for it: back to what worked
            if (!try_hollow(d, body_uu, t, open, was)) {
                m_workspace_browser->show_toast("Walls that thick don't fit this object");
                sp->set_value(was);
                return;
            }
            auto shell = find_hollow(d, body_uu);
            m_core.set_needs_save();
            m_object_editor_keep = true; // made again once the clicks are over
            m_core.rebuild("hollow");
            canvas_update_keep_selection();
            m_workspace_browser->update_documents(get_current_document_views());
            // show it: the view only goes up to the current group
            const auto current = m_core.get_current_idocument_info().get_uuid() == doc_uu
                                         ? m_core.get_current_group()
                                         : get_current_document_views()[doc_uu].m_current_group;
            const bool go_to_last =
                    shell && d.get_groups().contains(current) && shell->get_index() > d.get_group(current).get_index();
            schedule_object_panel_refresh(go_to_last ? 300 : 700, go_to_last);
        };
        sp->signal_value_changed().connect(apply);
        dd->property_selected().signal_changed().connect(apply);
        grid_attach_label_and_widget(*grid, "Hollow", *sp, top).add_css_class("tnum");
        grid_attach_label_and_widget(*grid, "Open side", *dd, top);
        if (hollow && !hollow->get_solid_model()) {
            auto status = Gtk::make_managed<Gtk::Label>("Can't hollow it out like this - try a thinner wall");
            status->add_css_class("dim-label");
            status->set_xalign(0);
            status->set_wrap(true);
            grid->attach(*status, 1, top++, 1, 1);
        }
    }

    append_object_parts(*grid, top, doc_uu, body_uu);

    // stickers: a picture on the surface
    if (std::ranges::any_of(get_body_solids(doc, UUID()), [&body_uu](const auto &bs) { return bs.body == body_uu; })) {
        heading("Stickers");
        auto decal_changed = [this, doc_uu, body_uu](const char *what) {
            m_core.set_needs_save();
            m_object_editor_keep = true;
            m_core.rebuild(what);
            canvas_update_keep_selection();
        };
        const auto &decals = body_group.m_body->m_decals;
        for (size_t i = 0; i < decals.size(); i++) {
            const auto &decal = decals.at(i);
            const UUID decal_uu = decal.m_uuid;
            auto find = [this, doc_uu, body_uu, decal_uu]() -> Decal * {
                auto &d = m_core.get_idocument_info(doc_uu).get_document();
                for (auto &it : d.get_group(body_uu).m_body->m_decals) {
                    if (it.m_uuid == decal_uu)
                        return &it;
                }
                return nullptr;
            };
            const auto n = decals.size() > 1 ? " " + std::to_string(i + 1) : std::string();
            const char *kind = decal.m_repeat ? "Pattern" : "Sticker";
            {
                auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
                auto sp = Gtk::make_managed<SpinButtonDim>();
                sp->set_range(0.1, 100000);
                sp->set_increments(1, 10);
                sp->set_value(decal.m_width);
                sp->set_hexpand(true);
                sp->set_width_chars(7); // leaves room for the remove button within the sidebar
                sp->set_tooltip_text(decal.m_repeat ? "Size of one tile" : "Width; the height follows the picture");
                sp->signal_value_changed().connect([sp, find, decal_changed] {
                    auto dc = find();
                    if (!dc || dc->m_width == sp->get_value())
                        return;
                    dc->m_width = sp->get_value();
                    decal_changed("sticker size");
                });
                box->append(*sp);
                auto del = Gtk::make_managed<Gtk::Button>();
                del->set_icon_name("edit-delete-symbolic");
                del->set_tooltip_text(decal.m_repeat ? "Remove this pattern" : "Remove this sticker");
                del->signal_clicked().connect([this, doc_uu, body_uu, decal_uu] {
                    Glib::signal_idle().connect_once([this, doc_uu, body_uu, decal_uu] {
                        auto &d = m_core.get_idocument_info(doc_uu).get_document();
                        std::erase_if(d.get_group(body_uu).m_body->m_decals,
                                      [&decal_uu](const auto &x) { return x.m_uuid == decal_uu; });
                        m_core.set_needs_save();
                        m_core.rebuild("remove sticker");
                        canvas_update_keep_selection();
                        show_object_editor(doc_uu, body_uu);
                    });
                });
                box->append(*del);
                grid_attach_label_and_widget(*grid, kind + n, *box, top).add_css_class("tnum");
            }
            {
                auto sp = Gtk::make_managed<Gtk::SpinButton>();
                sp->set_range(-360, 360);
                sp->set_increments(15, 45);
                sp->set_digits(0);
                sp->set_value(decal.m_angle);
                sp->set_hexpand(true);
                sp->signal_value_changed().connect([sp, find, decal_changed] {
                    auto dc = find();
                    if (!dc || dc->m_angle == sp->get_value())
                        return;
                    dc->m_angle = sp->get_value();
                    decal_changed("rotate sticker");
                });
                grid_attach_label_and_widget(*grid, "Rotation" + n, *sp, top).add_css_class("tnum");
            }
        }
        auto add = Gtk::make_managed<Gtk::Button>("Add sticker...");
        add->set_tooltip_text("Put a picture on the object's surface");
        add->signal_clicked().connect([this, doc_uu, body_uu] { choose_decal_picture(doc_uu, body_uu); });
        grid->attach(*add, 0, top++, 2, 1);

        // patterns: stripes, dots ... repeating over the surface
        auto pattern_names = Gtk::StringList::create();
        for (const auto &p : get_all_patterns())
            pattern_names->append(p.label);
        auto colour_names = Gtk::StringList::create();
        for (const auto &c : get_all_pattern_colours())
            colour_names->append(c.label);
        auto pattern_dd = Gtk::make_managed<Gtk::DropDown>(pattern_names);
        pattern_dd->set_hexpand(true);
        auto colour_dd = Gtk::make_managed<Gtk::DropDown>(colour_names);
        colour_dd->set_hexpand(true);
        auto prow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
        prow->append(*pattern_dd);
        prow->append(*colour_dd);
        grid_attach_label_and_widget(*grid, "Pattern", *prow, top);
        auto add_pattern = Gtk::make_managed<Gtk::Button>("Add pattern...");
        add_pattern->set_tooltip_text("Cover the surface with stripes, dots, checks or a grid; then click on the object");
        add_pattern->signal_clicked().connect([this, doc_uu, body_uu, pattern_dd, colour_dd] {
            begin_place_pattern(doc_uu, body_uu, pattern_dd->get_selected(), colour_dd->get_selected());
        });
        grid->attach(*add_pattern, 0, top++, 2, 1);
    }

    // combine with another shown object that has a solid
    {
        std::vector<std::pair<UUID, std::string>> others;
        for (const auto &bs : get_body_solids(doc, UUID(), [this](const UUID &b) { return body_is_shown(b); })) {
            if (bs.body != body_uu)
                others.emplace_back(bs.body, doc.get_group(bs.body).m_body->m_name);
        }
        const bool has_solid = std::ranges::any_of(get_body_solids(doc, UUID()),
                                                   [&body_uu](const auto &bs) { return bs.body == body_uu; });
        if (has_solid && others.size()) {
            heading("Combine");
            auto names = Gtk::StringList::create();
            for (const auto &[uu, name] : others)
                names->append(name);
            auto dd = Gtk::make_managed<Gtk::DropDown>(names);
            dd->set_hexpand(true);
            m_object_combine_dropdown = dd;
            grid_attach_label_and_widget(*grid, "With", *dd, top);
            auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
            box->add_css_class("linked");
            using Op = IGroupSolidModel::Operation;
            for (const auto &[label, tip, op] : std::vector<std::tuple<const char *, const char *, Op>>{
                         {"Join", "Merge both into one object", Op::UNION},
                         {"Subtract", "Cut the other object away from this one", Op::DIFFERENCE},
                         {"Intersect", "Keep only where they overlap", Op::INTERSECTION},
                 }) {
                auto bu = Gtk::make_managed<Gtk::Button>(label);
                bu->set_tooltip_text(tip);
                bu->set_hexpand(true);
                bu->signal_clicked().connect([this, dd, others, doc_uu, body_uu, op] {
                    const auto other = others.at(dd->get_selected()).first;
                    Glib::signal_idle().connect_once(
                            [this, doc_uu, body_uu, other, op] { combine_bodies(doc_uu, body_uu, other, op); });
                });
                box->append(*bu);
            }
            grid->attach(*box, 0, top++, 2, 1);
        }
    }

    // a row of copies along X (objects with an X/Y position, i.e. the ready-made shapes)
    if (has_xy_position(doc, body_uu)) {
        heading("Copies in a row");
        auto count = Gtk::make_managed<Gtk::SpinButton>();
        count->set_range(2, 50);
        count->set_increments(1, 5);
        count->set_digits(0);
        count->set_value(3);
        count->set_hexpand(true);
        grid_attach_label_and_widget(*grid, "How many", *count, top);
        auto gap = Gtk::make_managed<SpinButtonDim>();
        gap->set_range(0, 1000);
        gap->set_increments(1, 10);
        gap->set_value(5);
        gap->set_hexpand(true);
        grid_attach_label_and_widget(*grid, "Gap", *gap, top);
        auto make = Gtk::make_managed<Gtk::Button>("Make copies");
        make->set_tooltip_text("Add copies side by side along X, the gap apart; each is a full object of its own");
        make->signal_clicked().connect([this, doc_uu, body_uu, count, gap] {
            const unsigned int n = count->get_value_as_int();
            const double g = gap->get_value();
            Glib::signal_idle().connect_once([this, doc_uu, body_uu, n, g] { copy_in_a_row(doc_uu, body_uu, n, g); });
        });
        grid->attach(*make, 0, top++, 2, 1);
    }

    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
        box->set_margin_top(10);
        auto features = Gtk::make_managed<Gtk::Button>("Features");
        features->set_tooltip_text("Show the object's last feature");
        features->set_hexpand(true);
        const UUID last_group = groups.size() ? groups.back()->m_uuid : body_uu;
        features->signal_clicked().connect([this, doc_uu, last_group] {
            if (m_object_editor) {
                remove_from_side_panel(*m_object_editor);
                m_object_editor = nullptr;
            }
            on_workspace_browser_group_selected(doc_uu, last_group);
            m_workspace_browser->select_group(last_group);
            update_group_editor();
        });
        box->append(*features);
        auto dup = Gtk::make_managed<Gtk::Button>("Duplicate");
        dup->set_tooltip_text("Make a copy next to it (Ctrl+D)");
        dup->set_hexpand(true);
        dup->signal_clicked().connect([this, doc_uu, body_uu] {
            Glib::signal_idle().connect_once([this, doc_uu, body_uu] { duplicate_object(doc_uu, body_uu); });
        });
        box->append(*dup);
        auto del = Gtk::make_managed<Gtk::Button>("Delete");
        del->add_css_class("destructive-action");
        del->set_hexpand(true);
        del->set_sensitive(body_group.can_delete());
        del->signal_clicked().connect([this, doc_uu, body_uu] {
            // run after the click handler returns, the panel goes away
            Glib::signal_idle().connect_once([this, doc_uu, body_uu] { delete_body(doc_uu, body_uu); });
        });
        box->append(*del);
        grid->attach(*box, 0, top++, 2, 1);
    }
    {
        auto orbit = Gtk::make_managed<Gtk::Button>("Rotate view around it");
        orbit->set_tooltip_text("Make the middle of this object the centre of the view, so dragging with the middle "
                                "mouse button turns the view around it");
        orbit->set_margin_top(5);
        orbit->signal_clicked().connect([this, body_uu] {
            if (!m_core.has_documents())
                return;
            if (const auto bounds = get_body_bounds(m_core.get_current_document(), body_uu))
                get_canvas().animate_to_center_abs(glm::vec3((bounds->lo + bounds->hi) / 2.0));
        });
        grid->attach(*orbit, 0, top++, 2, 1);
    }

    add_panel_hints(*grid);
    auto cards = make_cards(*grid);
    m_object_editor = cards;
    m_group_editor_box->append(*cards);
    wheel_scrolls_past_spinbuttons(*cards);
    if (focus_name.size()) {
        for (auto card : get_object_editor_grids()) {
            for (auto ch = card->get_first_child(); ch; ch = ch->get_next_sibling()) {
                if (ch->get_name().raw() == focus_name)
                    ch->grab_focus();
            }
        }
    }
    else if (focus_label.size()) {
        for (auto card : get_object_editor_grids()) {
            for (auto ch = card->get_first_child(); ch; ch = ch->get_next_sibling()) {
                auto la = dynamic_cast<Gtk::Label *>(ch);
                if (!la || la->get_text().raw() != focus_label)
                    continue;
                int col, row, w, h;
                gtk_grid_query_child(card->gobj(), GTK_WIDGET(la->gobj()), &col, &row, &w, &h);
                if (auto field = card->get_child_at(col + 1, row))
                    field->grab_focus();
            }
        }
    }
    else {
        get_canvas().grab_focus(); // not the first entry of the new panel
    }
    restore_side_panel_scroll(scroll);
    canvas_update_keep_selection(); // highlight it
}

void Editor::duplicate_object(const UUID &doc_uu, const UUID &body_uu)
{
    if (m_core.tool_is_active())
        return;
    auto &doc = m_core.get_idocument_info(doc_uu).get_document();
    m_core.set_current_document(doc_uu);
    // next to everything else, keeping its y
    glm::dvec2 offset{0, 0};
    {
        Bnd_Box box;
        for (const auto &bs : get_body_solids(doc, UUID(), [&body_uu](const UUID &b) { return b == body_uu; }))
            BRepBndLib::Add(bs.solid->m_shape_acc, box, true); // by the triangles: meshes have no surfaces
        if (!box.IsVoid()) {
            double x0, y0, z0, x1, y1, z1;
            box.Get(x0, y0, z0, x1, y1, z1);
            const auto spot = find_free_spot(doc, doc.get_groups_sorted().back()->m_uuid, x1 - x0);
            offset.x = std::round(spot.x - (x0 + x1) / 2);
        }
    }
    auto &copy = duplicate_body(doc, body_uu, offset);
    const auto last = doc.get_groups_sorted().back()->m_uuid;
    get_current_document_view().m_current_group = last;
    m_core.set_current_group(last);
    m_core.set_needs_save();
    m_core.rebuild("duplicate object");
    m_workspace_browser->update_documents(get_current_document_views());
    set_current_group(last);
    if (!m_workspace_browser->select_body(doc_uu, copy.m_uuid))
        show_object_editor(doc_uu, copy.m_uuid);
    zoom_to_fit(true);
}

void Editor::copy_in_a_row(const UUID &doc_uu, const UUID &body_uu, unsigned int count, double gap)
{
    if (m_core.tool_is_active() || count < 2)
        return;
    auto &doc = m_core.get_idocument_info(doc_uu).get_document();
    m_core.set_current_document(doc_uu);
    double width = 0;
    {
        Bnd_Box box;
        for (const auto &bs : get_body_solids(doc, UUID(), [&body_uu](const UUID &b) { return b == body_uu; }))
            BRepBndLib::Add(bs.solid->m_shape_acc, box, true);
        if (box.IsVoid())
            return;
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        width = x1 - x0;
    }
    const double step = std::round((width + gap) * 10) / 10;
    UUID last_copy;
    for (unsigned int i = 1; i < count; i++)
        last_copy = duplicate_body(doc, body_uu, {step * i, 0}).m_uuid;
    const auto last = doc.get_groups_sorted().back()->m_uuid;
    get_current_document_view().m_current_group = last;
    m_core.set_current_group(last);
    m_core.set_needs_save();
    m_core.rebuild("copies in a row");
    m_workspace_browser->update_documents(get_current_document_views());
    set_current_group(last);
    if (!m_workspace_browser->select_body(doc_uu, last_copy))
        show_object_editor(doc_uu, last_copy);
    zoom_to_fit(true);
}

void Editor::combine_bodies(const UUID &doc_uu, const UUID &body_a, const UUID &body_b,
                            IGroupSolidModel::Operation op)
{
    if (m_core.tool_is_active())
        return;
    auto &doc = m_core.get_idocument_info(doc_uu).get_document();
    m_core.set_current_document(doc_uu);
    auto &group = add_combination(doc, body_a, body_b, op);
    // the result replaces the two on screen; they stay in the tree for editing
    for (const auto &b : {body_a, body_b})
        get_current_document_views()[doc_uu].m_body_views[b].m_visible = false;
    get_current_document_view().m_current_group = group.m_uuid;
    m_core.set_current_group(group.m_uuid);
    m_core.set_needs_save();
    m_core.rebuild("combine objects");
    m_workspace_browser->update_documents(get_current_document_views());
    set_current_group(group.m_uuid);
    if (!m_workspace_browser->select_body(doc_uu, group.m_uuid))
        show_object_editor(doc_uu, group.m_uuid);
    canvas_update_keep_selection();
}

void Editor::cut_with_hole(const UUID &doc_uu, const UUID &hole)
{
    if (m_core.tool_is_active())
        return;
    auto &doc = m_core.get_idocument_info(doc_uu).get_document();
    m_core.set_current_document(doc_uu);
    Bnd_Box hole_box;
    for (const auto &bs : get_body_solids(doc, UUID(), [&hole](const UUID &b) { return b == hole; }))
        BRepBndLib::Add(bs.solid->m_shape_acc, hole_box, true);
    std::vector<UUID> targets;
    if (!hole_box.IsVoid()) {
        for (const auto &bs : get_body_solids(doc, UUID(), [&](const UUID &b) {
                 return b != hole && !doc.get_group(b).m_body->m_hole && body_is_shown(b);
             })) {
            Bnd_Box box;
            BRepBndLib::Add(bs.solid->m_shape_acc, box, true);
            if (!box.IsOut(hole_box))
                targets.push_back(bs.body);
        }
    }
    if (targets.empty()) {
        m_workspace_browser->show_toast("The hole doesn't overlap any shown object");
        return;
    }
    UUID last;
    for (const auto &target : targets) {
        last = add_combination(doc, target, hole, IGroupSolidModel::Operation::DIFFERENCE).m_uuid;
        get_current_document_views()[doc_uu].m_body_views[target].m_visible = false;
    }
    // the hole has done its job; it stays in the tree, hidden
    get_current_document_views()[doc_uu].m_body_views[hole].m_visible = false;
    get_current_document_view().m_current_group = last;
    m_core.set_current_group(last);
    m_core.set_needs_save();
    m_core.rebuild("cut with hole");
    m_workspace_browser->update_documents(get_current_document_views());
    set_current_group(last);
    if (!m_workspace_browser->select_body(doc_uu, last))
        show_object_editor(doc_uu, last);
    canvas_update_keep_selection();
}

/*
 * On the face of a ready-made shape (one with an X/Y position), a click picks
 * the object unless the pointer is right on a line: otherwise the hover search,
 * which snaps to lines and points up to 10 px away, makes small objects like a
 * 6 mm hole almost impossible to pick or drag.
 */
bool Editor::ready_made_object_under_cursor()
{
    if (!m_core.has_documents() || !get_canvas().is_face_under_cursor())
        return false;
    const auto [origin, dir] = get_canvas().get_cursor_ray();
    auto &doc = m_core.get_current_document();
    const auto body = pick_body(doc, m_core.get_current_group(), origin, dir,
                                [this](const UUID &b) { return body_is_shown(b); });
    return body && can_move_object(doc, *body);
}

void Editor::select_object_under_cursor()
{
    if (!m_core.has_documents())
        return;
    const auto [origin, dir] = get_canvas().get_cursor_ray();
    auto &doc = m_core.get_current_document();
    if (auto body = pick_body(doc, m_core.get_current_group(), origin, dir,
                              [this](const UUID &b) { return body_is_shown(b); })) {
        // selecting the row shows the object panel, like clicking it in the browser
        const auto doc_uu = m_core.get_current_idocument_info().get_uuid();
        go_to_last_group(doc_uu);
        if (!m_workspace_browser->select_body(doc_uu, *body))
            show_object_editor(doc_uu, *body);
        begin_object_drag(doc_uu, *body); // in case the button stays down

    }
    else if (m_object_editor) {
        // clicked into empty space: let go of the object
        close_object_editor();
    }
}

void Editor::close_object_editor()
{
    if (!m_object_editor)
        return;
    update_group_editor();
    m_workspace_browser->select_group(m_core.get_current_group());
    canvas_update_keep_selection();
}

void Editor::delete_body(const UUID &doc_uu, const UUID &body_uu)
{
    if (m_core.tool_is_active())
        return;
    auto &doc = m_core.get_idocument_info(doc_uu).get_document();
    const auto body_groups = doc.get_groups_by_body();
    auto it = std::ranges::find_if(body_groups, [&body_uu](const auto &bg) { return bg.get_group().m_uuid == body_uu; });
    if (it == body_groups.end() || !doc.get_group(body_uu).can_delete())
        return;

    ItemsToDelete items;
    for (auto gr : it->groups)
        items.groups.insert(gr->m_uuid);

    // stay where we are unless that's part of the object; then go to the last group
    // left, so nothing after it gets hidden by moving back in the history
    UUID new_current;
    const auto current = m_core.get_current_idocument_info().get_uuid() == doc_uu
                                 ? m_core.get_current_group()
                                 : get_current_document_views()[doc_uu].m_current_group;
    if (current && !items.groups.contains(current) && doc.get_groups().contains(current)) {
        new_current = current;
    }
    else {
        for (auto gr : doc.get_groups_sorted()) {
            if (!items.groups.contains(gr->m_uuid))
                new_current = gr->m_uuid;
        }
    }
    if (!new_current)
        return;

    if (m_object_editor) {
        remove_from_side_panel(*m_object_editor);
        m_object_editor = nullptr;
    }
    CanvasUpdater canvas_updater{*this};
    m_core.set_current_document(doc_uu);
    // before deleting: pending-update bookkeeping may still point into the object
    doc.set_group_generate_pending(new_current);
    const auto items_initial = items;
    items.append(doc.get_additional_items_to_delete(items));
    show_delete_items_popup(items_initial, items);
    doc.delete_items(items);

    get_current_document_view().m_current_group = new_current;
    m_core.set_current_group(new_current);
    m_workspace_browser->update_documents(get_current_document_views());
    set_current_group(new_current);
    m_core.set_needs_save();
    m_core.rebuild("delete object");
}

} // namespace dune3d

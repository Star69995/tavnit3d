#include "editor.hpp"
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
#include "util/util.hpp"
#include "document/face_sketch.hpp"
#include "document/body_solids.hpp"
#include "document/combine.hpp"
#include "document/duplicate.hpp"
#include "document/primitives.hpp"
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
// friendlier names for the dimensions the ready-made shapes use
struct DatumInfo {
    std::string label;
    double scale = 1; // shown value = datum * scale
};

DatumInfo datum_info(const Document &doc, const Constraint &co, const IConstraintDatum &datum, const Group &group,
                     bool body_is_revolved)
{
    using T = Constraint::Type;
    bool from_origin = false;   // horizontal/vertical distance from a workplane's origin: a position
    bool on_own_plane = false;  // on a plane made in the same group: an upright profile
    if (auto pd = dynamic_cast<const ConstraintPointDistanceBase *>(&co)) {
        auto en = doc.get_entity_ptr(pd->m_entity1.entity);
        from_origin = en && en->get_type() == Entity::Type::WORKPLANE;
        if (auto wp = doc.get_entity_ptr(pd->m_wrkpl))
            on_own_plane = wp->m_group == co.m_group;
    }
    switch (co.get_type()) {
    case T::POINT_DISTANCE_HORIZONTAL:
        if (from_origin)
            return {"X"};
        if (on_own_plane)
            return {body_is_revolved ? "Radius" : "Width"};
        return {"Width"};
    case T::POINT_DISTANCE_VERTICAL:
        if (from_origin)
            return {"Y"};
        if (on_own_plane)
            return {"Height"};
        return {"Depth"};
    case T::DIAMETER:
        return {"Diameter"};
    case T::RADIUS:
        return {"Radius"};
    case T::POINT_DISTANCE:
        if (auto ex = dynamic_cast<const GroupExtrude *>(&group)) {
            // a symmetric extrusion goes both ways: show the whole thickness
            if (ex->m_mode == GroupExtrude::Mode::OFFSET_SYMMETRIC)
                return {"Depth", 2};
            return {"Height"};
        }
        break;
    default:;
    }
    return {datum.get_datum_name()};
}

int label_order(const std::string &label)
{
    static const std::vector<std::string> order = {"Width",          "Depth",          "Height", "Diameter",
                                                   "Outer diameter", "Inner diameter", "Radius", "X",
                                                   "Y"};
    auto it = std::ranges::find(order, label);
    return it - order.begin();
}
} // namespace

void Editor::show_object_editor(const UUID &doc_uu, const UUID &body_uu)
{
    if (m_group_editor) {
        if (m_delayed_commit_connection.connected())
            commit_from_editor();
        m_group_editor_box->remove(*m_group_editor);
        m_group_editor = nullptr;
    }
    if (m_object_editor) {
        m_group_editor_box->remove(*m_object_editor);
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
    m_object_editor_body = body_uu;
    m_object_combine_dropdown = nullptr;

    auto grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(5);
    grid->set_column_spacing(5);
    grid->set_margin(10);
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
        auto items = Gtk::StringList::create({"Satin", "Matte", "Glossy", "Metal"});
        auto dd = Gtk::make_managed<Gtk::DropDown>(items);
        dd->set_selected(static_cast<guint>(body_group.m_body->m_material));
        dd->property_selected().signal_changed().connect([this, dd, doc_uu, body_uu, changed] {
            auto &d = m_core.get_idocument_info(doc_uu).get_document();
            d.get_group(body_uu).m_body->m_material = static_cast<Body::Material>(dd->get_selected());
            changed(body_uu, false);
        });
        grid_attach_label_and_widget(*grid, "Material", *dd, top);
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

    // every dimension in the object's groups, editable in place
    const auto body_groups = doc.get_groups_by_body();
    auto it = std::ranges::find_if(body_groups, [&body_uu](const auto &bg) { return bg.get_group().m_uuid == body_uu; });
    std::vector<const Group *> groups;
    if (it != body_groups.end())
        groups = it->groups;
    struct Dim {
        std::string label;
        UUID constraint;
        UUID group;
        double scale;
        double value;
    };
    const bool revolved = std::ranges::any_of(groups, [](auto gr) { return gr->get_type() == Group::Type::REVOLVE; });
    std::vector<Dim> dims;
    for (auto gr : groups) {
        for (auto &[uu, co] : doc.m_constraints) {
            if (co->m_group != gr->m_uuid)
                continue;
            auto datum = dynamic_cast<IConstraintDatum *>(co.get());
            if (!datum || datum->is_measurement())
                continue;
            const auto info = datum_info(doc, *co, *datum, *gr, revolved);
            dims.push_back({info.label, uu, gr->m_uuid, info.scale, datum->get_datum() * info.scale});
        }
    }
    // two diameters (a tube): outer and inner
    {
        std::vector<Dim *> diameters;
        for (auto &d : dims) {
            if (d.label == "Diameter")
                diameters.push_back(&d);
        }
        if (diameters.size() == 2) {
            std::ranges::sort(diameters, {}, [](auto d) { return -d->value; });
            diameters.at(0)->label = "Outer diameter";
            diameters.at(1)->label = "Inner diameter";
        }
    }
    std::ranges::stable_sort(dims, {}, [](const Dim &d) { return label_order(d.label); });
    if (dims.size())
        heading("Dimensions");
    for (const auto &dim : dims) {
        auto datum = dynamic_cast<IConstraintDatum *>(doc.m_constraints.at(dim.constraint).get());
        Gtk::SpinButton *sp = nullptr;
        if (datum->get_datum_unit() == DatumUnit::MM) {
            sp = Gtk::make_managed<SpinButtonDim>();
        }
        else {
            sp = Gtk::make_managed<Gtk::SpinButton>();
            sp->set_digits(datum->get_datum_unit() == DatumUnit::INTEGER ? 0 : 3);
            sp->set_increments(1, 10);
        }
        const auto [lo, hi] = datum->get_datum_range();
        sp->set_range(lo * dim.scale, hi * dim.scale);
        sp->set_value(dim.value);
        sp->set_hexpand(true);
        const UUID co_uu = dim.constraint;
        const UUID group_uu = dim.group;
        const double scale = dim.scale;
        sp->signal_activate().connect([this, sp, doc_uu, co_uu, group_uu, scale, changed] {
            sp->update();
            auto &d = m_core.get_idocument_info(doc_uu).get_document();
            auto &c = *d.m_constraints.at(co_uu);
            dynamic_cast<IConstraintDatum &>(c).set_datum(sp->get_value() / scale);
            changed(group_uu, true);
        });
        grid_attach_label_and_widget(*grid, dim.label, *sp, top).add_css_class("tnum");
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

    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
        box->set_margin_top(10);
        auto features = Gtk::make_managed<Gtk::Button>("Features");
        features->set_tooltip_text("Show the object's last feature");
        features->set_hexpand(true);
        const UUID last_group = groups.size() ? groups.back()->m_uuid : body_uu;
        features->signal_clicked().connect([this, doc_uu, last_group] {
            if (m_object_editor) {
                m_group_editor_box->remove(*m_object_editor);
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

    m_group_editor_box->append(*grid);
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
            BRepBndLib::AddOptimal(bs.solid->m_shape_acc, box, false, false);
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
        m_group_editor_box->remove(*m_object_editor);
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

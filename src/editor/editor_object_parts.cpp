#include "editor.hpp"
#include "dune3d_appwindow.hpp"
#include "workspace_browser.hpp"
#include "canvas/canvas.hpp"
#include "document/document.hpp"
#include "document/object_parts.hpp"
#include "document/object_dimensions.hpp"
#include "document/round_edges.hpp"
#include "document/group/group_fillet.hpp"
#include "document/constraint/constraint.hpp"
#include "document/constraint/iconstraint_datum.hpp"
#include "widgets/spin_button_dim.hpp"
#include "util/gtk_util.hpp"
#include <ranges>

namespace dune3d {

/*
 * The object panel's "Parts" for a joined object (Combine, or shapes an AI
 * joined, like a table): each part's sizes and rounding, parts that are alike
 * (the legs) changed together unless split up, the parts touching a changed one
 * following it; and X/Y for the whole object, which moves all of its parts.
 * Picking a part shows its resize handles instead of the whole object's.
 */

Gtk::SpinButton &Editor::make_dimension_spin(const IConstraintDatum &datum, const ObjectDimension &dim)
{
    Gtk::SpinButton *sp = nullptr;
    if (datum.get_datum_unit() == DatumUnit::MM) {
        auto spd = Gtk::make_managed<SpinButtonDim>();
        spd->set_show_plus(dim.label == "X" || dim.label == "Y");
        sp = spd;
        sp->set_increments(1, 10); // +/- by whole millimetres; any value can be typed
    }
    else {
        sp = Gtk::make_managed<Gtk::SpinButton>();
        sp->set_digits(datum.get_datum_unit() == DatumUnit::INTEGER ? 0 : 3);
        sp->set_increments(1, 10);
    }
    const auto [lo, hi] = datum.get_datum_range();
    sp->set_range(lo * dim.scale, hi * dim.scale);
    sp->set_value(dim.value);
    sp->set_hexpand(true);
    return *sp;
}

std::vector<UUID> Editor::get_part_set(const Document &doc, const UUID &object, const UUID &part) const
{
    if (m_split_parts.contains(part))
        return {part};
    for (const auto &set : group_alike_parts(doc, get_object_parts(doc, object))) {
        if (std::ranges::find(set.parts, part) != set.parts.end())
            return set.parts;
    }
    return {part};
}

bool Editor::can_move_object(const Document &doc, const UUID &body) const
{
    if (std::ranges::any_of(get_object_dimensions(doc, body), [](const auto &d) { return d.label == "X"; }))
        return true;
    const auto parts = get_object_parts(doc, body);
    return parts.size()
           && std::ranges::all_of(parts, [&doc](const auto &p) { return get_part_position(doc, p).has_value(); });
}

void Editor::finish_parts_change(const UUID &doc_uu, const std::string &what)
{
    m_core.set_needs_save();
    // the panel stays as it is for now, so quick clicks on +/- all count ...
    m_object_editor_keep = true;
    m_core.rebuild(what);
    canvas_update_keep_selection();
    m_workspace_browser->update_documents(get_current_document_views());
    // ... and shows what moved along once they're done
    schedule_object_panel_refresh(700);
}

void Editor::append_object_parts(Gtk::Grid &grid, int &top, const UUID &doc_uu, const UUID &body_uu)
{
    auto &doc = m_core.get_idocument_info(doc_uu).get_document();
    const auto parts = get_object_parts(doc, body_uu);
    if (parts.empty())
        return;

    auto heading = [&grid, &top](const std::string &text) {
        auto la = Gtk::make_managed<Gtk::Label>(text);
        la->set_xalign(0);
        la->add_css_class("object-editor-heading");
        grid.attach(*la, 0, top++, 2, 1);
    };

    // the whole object: where its middle is
    if (can_move_object(doc, body_uu)) {
        if (auto bounds = get_body_bounds(doc, body_uu)) {
            heading("Position");
            const auto mid = (bounds->lo + bounds->hi) / 2.;
            for (int axis = 0; axis < 2; axis++) {
                auto sp = Gtk::make_managed<SpinButtonDim>();
                sp->set_show_plus(true);
                sp->set_range(-1e5, 1e5);
                sp->set_increments(1, 10);
                sp->set_value(mid[axis]);
                sp->set_hexpand(true);
                sp->set_name(axis ? "object-y" : "object-x");
                sp->signal_value_changed().connect([this, sp, doc_uu, body_uu, axis] {
                    auto &d = m_core.get_idocument_info(doc_uu).get_document();
                    // from where it is now: the field may be behind by a change
                    const auto b = get_body_bounds(d, body_uu);
                    if (!b)
                        return;
                    glm::dvec3 delta{0, 0, 0};
                    delta[axis] = sp->get_value() - (b->lo[axis] + b->hi[axis]) / 2;
                    if (std::abs(delta[axis]) < 1e-9)
                        return;
                    for (const auto &p : get_object_parts(d, body_uu)) {
                        if (auto pos = get_part_position(d, p))
                            set_part_position(d, *pos, delta);
                    }
                    finish_parts_change(doc_uu, "move object");
                });
                grid_attach_label_and_widget(grid, axis ? "Y" : "X", *sp, top).add_css_class("tnum");
            }
        }
    }

    heading("Parts");
    // a part, or parts that are alike, with their sizes and rounding
    auto add_block = [&](const std::vector<UUID> &set, const std::string &title, const char *split_label) {
        const auto first = set.front();
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
        auto pick = Gtk::make_managed<Gtk::ToggleButton>(title);
        pick->set_hexpand(true);
        pick->set_tooltip_text("Show this part's handles, to change it by dragging");
        pick->set_active(std::ranges::find(set, m_object_editor_part) != set.end());
        pick->signal_toggled().connect([this, pick, first, doc_uu, body_uu] {
            m_object_editor_part = pick->get_active() ? first : UUID();
            canvas_update_keep_selection();
            Glib::signal_idle().connect_once([this, doc_uu, body_uu] { show_object_editor(doc_uu, body_uu); });
        });
        row->append(*pick);
        if (split_label) {
            auto split = Gtk::make_managed<Gtk::Button>(split_label);
            const bool together = std::string(split_label) == "Together";
            split->set_tooltip_text(together ? "Change these parts together again"
                                             : "Change each of these parts on its own");
            std::vector<UUID> all = set;
            if (together) {
                // all the parts like this one
                all.clear();
                for (const auto &s : group_alike_parts(doc, parts)) {
                    if (std::ranges::find(s.parts, first) != s.parts.end())
                        all = s.parts;
                }
            }
            split->signal_clicked().connect([this, all, together, doc_uu, body_uu] {
                for (const auto &p : all) {
                    if (together)
                        m_split_parts.erase(p);
                    else
                        m_split_parts.insert(p);
                }
                if (std::ranges::find(all, m_object_editor_part) != all.end())
                    m_object_editor_part = all.front();
                Glib::signal_idle().connect_once([this, doc_uu, body_uu] { show_object_editor(doc_uu, body_uu); });
            });
            row->append(*split);
        }
        grid.attach(*row, 0, top++, 2, 1);

        // sizes; where each of several parts is differs, so that's only for one
        const auto dims = get_object_dimensions(doc, first);
        for (size_t i = 0; i < dims.size(); i++) {
            const auto &dim = dims.at(i);
            if (dim.label == "X" || dim.label == "Y")
                continue; // below, as for the whole object

            auto datum = dynamic_cast<const IConstraintDatum *>(doc.get_constraint_ptr(dim.constraint));
            if (!datum)
                continue;
            auto sp = &make_dimension_spin(*datum, dim);
            sp->set_name("part-" + (std::string)dim.constraint);
            sp->signal_value_changed().connect([this, sp, set, i, doc_uu, body_uu] {
                auto &d = m_core.get_idocument_info(doc_uu).get_document();
                const auto follow = begin_parts_follow(d, get_object_parts(d, body_uu), set);
                if (i >= follow.dims.size() || std::abs(follow.dims.at(i).value - sp->get_value()) < 1e-9)
                    return;
                set_parts_dimension(d, follow, i, sp->get_value());
                finish_parts_change(doc_uu, "change " + follow.dims.at(i).label);
            });
            grid_attach_label_and_widget(grid, dim.label, *sp, top).add_css_class("tnum");
        }
        // where one part is: X/Y of its middle, Z of its bottom (a raised one, like a table's top)
        if (set.size() == 1) {
            auto pos = get_part_position(doc, first);
            auto bounds = get_body_bounds(doc, first);
            for (int axis = 0; pos && bounds && axis < 3; axis++) {
                if (axis == 2 && !pos->wrkpl)
                    continue; // standing on the floor
                const double value = axis == 2 ? bounds->lo.z : (bounds->lo[axis] + bounds->hi[axis]) / 2;
                auto sp = Gtk::make_managed<SpinButtonDim>();
                sp->set_show_plus(true);
                sp->set_range(-1e5, 1e5);
                sp->set_increments(1, 10);
                sp->set_value(value);
                sp->set_hexpand(true);
                if (axis == 2)
                    sp->set_tooltip_text("How high its bottom is");
                sp->set_name("part-" + std::to_string(axis) + "-" + (std::string)first);
                sp->signal_value_changed().connect([this, sp, first, axis, doc_uu] {
                    auto &d = m_core.get_idocument_info(doc_uu).get_document();
                    auto p = get_part_position(d, first);
                    // from where it is now: the field may be behind by a change
                    const auto b = get_body_bounds(d, first);
                    if (!p || !b)
                        return;
                    glm::dvec3 delta{0, 0, 0};
                    delta[axis] = sp->get_value() - (axis == 2 ? b->lo.z : (b->lo[axis] + b->hi[axis]) / 2);
                    if (std::abs(delta[axis]) < 1e-9)
                        return;
                    set_part_position(d, *p, delta);
                    finish_parts_change(doc_uu, "move part");
                });
                grid_attach_label_and_widget(grid, axis == 0 ? "X" : axis == 1 ? "Y" : "Z", *sp, top)
                        .add_css_class("tnum");
            }
        }
        {
            auto sp = Gtk::make_managed<SpinButtonDim>();
            sp->set_range(0, 1000);
            sp->set_hexpand(true);
            auto fillet = find_round_edges(doc, first);
            sp->set_value(fillet ? fillet->m_radius : 0);
            sp->set_tooltip_text("Rounds all of its edges; 0 for sharp edges");
            sp->set_name("part-round-" + (std::string)first);
            sp->signal_value_changed().connect([this, sp, set, doc_uu] {
                auto &d = m_core.get_idocument_info(doc_uu).get_document();
                bool any = false;
                for (const auto &p : set) {
                    auto existing = find_round_edges(d, p);
                    if ((existing ? existing->m_radius : 0) == sp->get_value())
                        continue;
                    set_round_edges(d, p, sp->get_value());
                    any = true;
                }
                if (any)
                    finish_parts_change(doc_uu, "round edges");
            });
            grid_attach_label_and_widget(grid, "Round edges", *sp, top).add_css_class("tnum");
        }
    };

    for (const auto &set : group_alike_parts(doc, parts)) {
        const bool split = set.parts.size() > 1
                           && std::ranges::any_of(set.parts, [this](const auto &p) { return m_split_parts.contains(p); });
        if (set.parts.size() == 1) {
            add_block(set.parts, doc.get_group(set.parts.front()).m_body->m_name, nullptr);
        }
        else if (!split) {
            add_block(set.parts, set.name + " ×" + std::to_string(set.parts.size()), "Separately");
        }
        else {
            for (size_t i = 0; i < set.parts.size(); i++) {
                const auto &p = set.parts.at(i);
                add_block({p}, doc.get_group(p).m_body->m_name, i == 0 ? "Together" : nullptr);
            }
        }
    }
}

} // namespace dune3d

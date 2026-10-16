#include "group_editor_extrude.hpp"
#include "document/group/group_extrude.hpp"
#include "util/gtk_util.hpp"
#include "core/core.hpp"
#include "document/constraint/constraint_point_distance.hpp"
#include "widgets/spin_button_dim.hpp"

namespace dune3d {

GroupEditorExtrude::GroupEditorExtrude(Core &core, const UUID &group_uu) : GroupEditorSweep(core, group_uu)
{
    // Distance typed in directly, backed by a regular distance constraint on the
    // extrusion's leader line so it can still be edited on the canvas.
    m_height_sp = Gtk::make_managed<SpinButtonDim>();
    m_height_sp->set_range(0, 100000);
    m_height_sp->set_hexpand(true);
    reload_height();
    connect_spinbutton(*m_height_sp, sigc::mem_fun(*this, &GroupEditorExtrude::update_height));
    grid_attach_label_and_widget(*this, "Distance", *m_height_sp, m_top).add_css_class("tnum");

    m_normal_switch = Gtk::make_managed<Gtk::Switch>();
    m_normal_switch->set_valign(Gtk::Align::CENTER);
    m_normal_switch->set_halign(Gtk::Align::START);
    auto &group = get_group();
    m_normal_switch->set_active(group.m_direction == GroupExtrude::Direction::NORMAL);
    grid_attach_label_and_widget(*this, "Along normal", *m_normal_switch, m_top);
    m_normal_switch->property_active().signal_changed().connect([this] {
        if (is_reloading())
            return;
        auto &group = get_group();
        if (m_normal_switch->get_active())
            group.m_direction = GroupExtrude::Direction::NORMAL;
        else
            group.m_direction = GroupExtrude::Direction::ARBITRARY;
        m_core.get_current_document().set_group_solve_pending(group.m_uuid);
        m_signal_changed.emit(CommitMode::IMMEDIATE);
    });
    add_operation_combo();
    {
        auto items = Gtk::StringList::create();
        items->append("Single");
        items->append("Offset");
        items->append("Offset symmetric");

        m_mode_combo = Gtk::make_managed<Gtk::DropDown>(items);
        m_mode_combo->set_selected(static_cast<guint>(group.m_mode));
        m_mode_combo->property_selected().signal_changed().connect([this] {
            if (is_reloading())
                return;
            auto &group = get_group();
            group.m_mode = static_cast<GroupExtrude::Mode>(m_mode_combo->get_selected());
            m_core.get_current_document().set_group_generate_pending(group.m_uuid);
            m_signal_changed.emit(CommitMode::IMMEDIATE);
        });
        grid_attach_label_and_widget(*this, "Mode", *m_mode_combo, m_top);
    }
}

void GroupEditorExtrude::do_reload()
{
    GroupEditorSweep::do_reload();
    auto &group = get_group();
    m_normal_switch->set_active(group.m_direction == GroupExtrude::Direction::NORMAL);
    m_mode_combo->set_selected(static_cast<guint>(group.m_mode));
    reload_height();
}

ConstraintPointDistance *GroupEditorExtrude::find_height_constraint()
{
    auto &doc = m_core.get_current_document();
    auto &group = get_group();
    const auto leader = group.get_leader_line_uuid(GroupExtrude::Side::TOP);
    for (auto &[uu, co] : doc.m_constraints) {
        if (co->m_group != group.m_uuid)
            continue;
        if (auto dist = dynamic_cast<ConstraintPointDistance *>(co.get())) {
            if (dist->m_entity1.entity == leader && dist->m_entity2.entity == leader)
                return dist;
        }
    }
    return nullptr;
}

void GroupEditorExtrude::reload_height()
{
    auto &group = get_group();
    if (auto co = find_height_constraint())
        m_height_sp->set_value(co->m_distance);
    else
        m_height_sp->set_value(glm::length(group.m_dvec));
    // with no constraint of ours, the height may already be pinned by one the user added
    m_height_sp->set_sensitive(find_height_constraint() || group.m_dof != 0);
}

bool GroupEditorExtrude::update_height()
{
    if (is_reloading())
        return false;
    auto &doc = m_core.get_current_document();
    auto &group = get_group();
    const double h = m_height_sp->get_value();
    if (h <= 0)
        return false;
    auto co = find_height_constraint();
    if (co && co->m_distance == h)
        return false;
    if (!co) {
        co = &doc.add_constraint<ConstraintPointDistance>(UUID::random());
        co->m_group = group.m_uuid;
        const auto leader = group.get_leader_line_uuid(GroupExtrude::Side::TOP);
        co->m_entity1 = {leader, 1};
        co->m_entity2 = {leader, 2};
    }
    co->m_distance = h;
    if (glm::length(group.m_dvec) > 0)
        group.m_dvec = glm::normalize(group.m_dvec) * h;
    doc.set_group_solve_pending(group.m_uuid);
    return true;
}

GroupExtrude &GroupEditorExtrude::get_group()
{
    return m_core.get_current_document().get_group<GroupExtrude>(m_group_uu);
}


} // namespace dune3d

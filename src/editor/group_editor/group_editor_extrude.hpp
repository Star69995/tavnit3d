#pragma once
#include "group_editor_sweep.hpp"

namespace dune3d {

class GroupExtrude;

class GroupEditorExtrude : public GroupEditorSweep {
public:
    GroupEditorExtrude(Core &core, const UUID &group_uu);

    void do_reload() override;

private:
    GroupExtrude &get_group();
    class ConstraintPointDistance *find_height_constraint();
    bool update_height();
    void reload_height();

    Gtk::Switch *m_normal_switch = nullptr;
    Gtk::DropDown *m_mode_combo = nullptr;
    class SpinButtonDim *m_height_sp = nullptr;
};

} // namespace dune3d

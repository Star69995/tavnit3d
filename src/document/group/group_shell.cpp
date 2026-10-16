#include "group_shell.hpp"
#include "nlohmann/json.hpp"
#include "util/lut.hpp"
#include "document/solid_model/solid_model.hpp"

namespace dune3d {

static const LutEnumStr<GroupShell::Open> open_lut = {
        {"top", GroupShell::Open::TOP},
        {"bottom", GroupShell::Open::BOTTOM},
        {"none", GroupShell::Open::NONE},
};

GroupShell::GroupShell(const UUID &uu) : Group(uu)
{
}

GroupShell::GroupShell(const UUID &uu, const json &j)
    : Group(uu, j), m_thickness(j.at("thickness").get<double>()),
      m_open(open_lut.lookup_opt(j.value("open", "top")).value_or(Open::TOP))
{
}

json GroupShell::serialize() const
{
    auto j = Group::serialize();
    j["thickness"] = m_thickness;
    j["open"] = open_lut.lookup_reverse(m_open);
    return j;
}

std::unique_ptr<Group> GroupShell::clone() const
{
    return std::make_unique<GroupShell>(*this);
}

std::list<GroupStatusMessage> GroupShell::get_messages() const
{
    auto msg = Group::get_messages();
    msg.insert(msg.end(), m_shell_messages.begin(), m_shell_messages.end());
    return msg;
}

const SolidModel *GroupShell::get_solid_model() const
{
    return m_solid_model.get();
}

void GroupShell::update_solid_model(const Document &doc)
{
    m_solid_model = SolidModel::create(doc, *this);
}

} // namespace dune3d

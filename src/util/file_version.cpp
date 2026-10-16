#include "file_version.hpp"
#include "nlohmann/json.hpp"
#include "logger/logger.hpp"
#include "util/uuid.hpp"

namespace dune3d {

FileVersion::FileVersion(unsigned int a) : m_app(a), m_file(a)
{
}

FileVersion::FileVersion(unsigned int a, unsigned int f) : m_app(a), m_file(f)
{
}

FileVersion::FileVersion(unsigned int a, const json &j) : m_app(a), m_file(j.value("version", 0))
{
}

void FileVersion::serialize(json &j) const
{
    if (m_app)
        j["version"] = m_app;
}

void FileVersion::update_file_from_app()
{
    m_file = m_app;
}

const std::string FileVersion::learn_more_markup =
        "<a href=\"https://docs.dune3d.org/en/latest/version.html\">Learn more</a>";

std::string FileVersion::get_message() const
{
    if (m_app > m_file) {
        return "This document uses an older file format. Saving will update it to the current format, which "
               "older versions of the app (and of Dune 3D) may not be able to open. "
               + learn_more_markup;
    }
    else if (m_file > m_app) {
        return "This document was made with a newer version of the app. Some content may not display "
               "correctly. To preserve fidelity, it has been opened read-only. "
               + learn_more_markup;
    }
    else {
        return "";
    }
}
} // namespace dune3d

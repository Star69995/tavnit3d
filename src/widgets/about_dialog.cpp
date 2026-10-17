#include "about_dialog.hpp"
#include "util/version.hpp"
#include "util/branding.hpp"

namespace dune3d {

AboutDialog::AboutDialog() : Gtk::AboutDialog()
{
    std::string version =
            std::string(app_version) + "\nbased on Dune 3D " + Version::get_string() + " " + Version::name;
    if (strlen(Version::commit)) {
        version += "\nCommit " + std::string(Version::commit);
    }
    set_version(version);
    set_program_name(app_name);
    std::vector<Glib::ustring> authors;
    authors.push_back("Lukas K. <lukas@dune3d.org> (Dune 3D)");
    authors.push_back("Star (Tavnit 3D)");
    set_authors(authors);
    add_credit_section("Tavnit 3D changes written mostly by", {"Claude, an AI model by Anthropic"});
    // "name url" entries become links
    add_credit_section("Source code", {std::string("Tavnit 3D on GitHub ") + source_url,
                                       std::string("Report a problem ") + issues_url});
    add_credit_section("Based on", {"Dune 3D https://dune3d.org/", "Dune 3D on GitHub https://github.com/dune3d/dune3d"});
    set_license_type(Gtk::License::GPL_3_0);
    set_copyright("Copyright © 2024 Lukas K., et al.");
    set_website(website_url);
    set_website_label("star69995.github.io/tavnit3d");
    set_comments("A friendly parametric 3D CAD.\nBased on Dune 3D (dune3d.org) by Lukas K. and contributors.\n"
                 "Tavnit 3D's changes were written mostly by Claude (Anthropic's AI), directed by Star.");

    set_logo_icon_name("dune3d");
}

} // namespace dune3d

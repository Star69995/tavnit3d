#include "describe_window.hpp"
#include "document/recipe_ai.hpp"
#include <glibmm/miscutils.h>

namespace dune3d {

DescribeWindow::DescribeWindow(Gtk::Window &parent, std::string &api_key, OnRecipe on_recipe)
    : m_api_key(api_key), m_on_recipe(std::move(on_recipe))
{
    set_title("Make from a description");
    add_css_class("studio-dialog");
    set_transient_for(parent);
    set_default_size(520, 380);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    box->set_margin(14);
    set_child(*box);

    auto intro = Gtk::make_managed<Gtk::Label>(
            "Describe what to make - shape, size, colour, material. Claude works out the shapes, and you can "
            "edit them afterwards like any other object.");
    intro->set_wrap(true);
    intro->set_xalign(0);
    box->append(*intro);

    auto sc = Gtk::make_managed<Gtk::ScrolledWindow>();
    sc->set_vexpand(true);
    sc->set_has_frame(true);
    m_text = Gtk::make_managed<Gtk::TextView>();
    m_text->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    m_text->set_top_margin(8);
    m_text->set_bottom_margin(8);
    m_text->set_left_margin(8);
    m_text->set_right_margin(8);
    m_text->get_buffer()->set_text("");
    sc->set_child(*m_text);
    box->append(*sc);
    auto examples = Gtk::make_managed<Gtk::Label>(
            "For example: \"a cereal bowl, light blue and glossy\", \"a mug with a handle\", \"a lamp shade 30 cm "
            "across\", \"a phone stand\"");
    examples->set_wrap(true);
    examples->set_xalign(0);
    examples->add_css_class("dim-label");
    box->append(*examples);

    m_key_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    {
        auto la = Gtk::make_managed<Gtk::Label>("Claude API key (kept on this computer only)");
        la->set_xalign(0);
        m_key_box->append(*la);
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        m_key_entry = Gtk::make_managed<Gtk::PasswordEntry>();
        m_key_entry->set_show_peek_icon(true);
        m_key_entry->set_hexpand(true);
        row->append(*m_key_entry);
        auto get = Gtk::make_managed<Gtk::Button>("Get a key");
        get->signal_clicked().connect([this] {
            Gtk::UriLauncher::create("https://console.anthropic.com/settings/keys")->launch(*this, [](auto &) {});
        });
        row->append(*get);
        m_key_box->append(*row);
    }
    box->append(*m_key_box);

    auto bbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    bbox->set_halign(Gtk::Align::CENTER);
    m_spinner = Gtk::make_managed<Gtk::Spinner>();
    bbox->append(*m_spinner);
    m_make_button = Gtk::make_managed<Gtk::Button>("Make it");
    m_make_button->add_css_class("suggested-action");
    m_make_button->signal_clicked().connect([this] { start(); });
    bbox->append(*m_make_button);
    box->append(*bbox);

    m_status = Gtk::make_managed<Gtk::Label>();
    m_status->set_wrap(true);
    m_status->add_css_class("dim-label");
    box->append(*m_status);

    m_dispatch_done.connect([this] { finish(); });
    update_key_box();
}

DescribeWindow::~DescribeWindow()
{
    m_closed = true;
    // an answer can take a while; don't wait for it
    if (m_thread.joinable())
        m_thread.detach();
}

std::string DescribeWindow::get_key() const
{
    if (m_api_key.size())
        return m_api_key;
    return Glib::getenv("ANTHROPIC_API_KEY");
}

void DescribeWindow::update_key_box()
{
    m_key_box->set_visible(get_key().empty());
}

void DescribeWindow::set_description(const std::string &text)
{
    m_text->get_buffer()->set_text(text);
}

void DescribeWindow::start()
{
    if (m_thread.joinable())
        return;
    if (get_key().empty()) {
        const std::string key = m_key_entry->get_text();
        if (key.empty()) {
            m_status->set_text("Claude needs an API key - get one with the button next to the field");
            return;
        }
        m_api_key = key;
        update_key_box();
    }
    m_description = m_text->get_buffer()->get_text();
    if (m_description.find_first_not_of(" \t\r\n") == std::string::npos) {
        m_status->set_text("Write what to make first");
        return;
    }
    m_make_button->set_sensitive(false);
    m_spinner->start();
    m_status->set_text("Claude is working on it... (this can take a minute)");
    m_thread = std::thread([this, key = get_key(), description = m_description] {
        auto answer = ask_for_recipe(key, description);
        if (m_closed)
            return;
        m_recipe = answer.recipe;
        m_error = answer.error;
        m_dispatch_done.emit();
    });
}

void DescribeWindow::finish()
{
    if (m_thread.joinable())
        m_thread.join();
    m_spinner->stop();
    m_make_button->set_sensitive(true);
    if (m_error.size()) {
        m_status->set_text(m_error);
        if (m_error.starts_with("The API key wasn't accepted")) {
            m_api_key.clear(); // ask again
            update_key_box();
        }
        return;
    }
    if (m_on_recipe(m_recipe, m_description))
        m_status->set_text("Done - it's in the drawing. Describe something else, or close this.");
    else
        m_status->set_text("Claude's recipe couldn't be built - try describing it differently");
}

} // namespace dune3d

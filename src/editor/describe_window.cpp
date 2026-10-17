#include "describe_window.hpp"
#include "document/recipe_ai.hpp"

namespace dune3d {

DescribeWindow::DescribeWindow(Gtk::Window &parent, AiSettings &settings, OnRecipe on_recipe)
    : m_settings(settings), m_on_recipe(std::move(on_recipe))
{
    set_title("Make from a description");
    add_css_class("studio-dialog");
    set_transient_for(parent);
    set_default_size(540, 480);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    box->set_margin(14);
    set_child(*box);

    auto intro = Gtk::make_managed<Gtk::Label>(
            "Describe what to make - shape, size, colour, material. An AI works out the shapes, and you can "
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

    // which service, and its key (kept per service, on this computer only)
    m_service = Gtk::make_managed<Gtk::Expander>("AI service and key");
    {
        auto sbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        sbox->set_margin_top(6);

        std::vector<Glib::ustring> labels;
        for (const auto &p : ai_providers())
            labels.push_back(p.label);
        m_provider = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create(labels));
        auto la = Gtk::make_managed<Gtk::Label>("Service");
        la->set_xalign(0);
        sbox->append(*la);
        sbox->append(*m_provider);

        auto lk = Gtk::make_managed<Gtk::Label>("API key (kept on this computer only)");
        lk->set_xalign(0);
        sbox->append(*lk);
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        m_key_entry = Gtk::make_managed<Gtk::PasswordEntry>();
        m_key_entry->set_show_peek_icon(true);
        m_key_entry->set_hexpand(true);
        row->append(*m_key_entry);
        m_get_key = Gtk::make_managed<Gtk::Button>("Get a key");
        m_get_key->signal_clicked().connect([this] {
            const auto &page = ai_provider(m_settings.provider).key_page;
            if (page.size())
                Gtk::UriLauncher::create(page)->launch(*this, [](auto &) {});
        });
        row->append(*m_get_key);
        sbox->append(*row);

        auto lm = Gtk::make_managed<Gtk::Label>("Model");
        lm->set_xalign(0);
        sbox->append(*lm);
        m_model_entry = Gtk::make_managed<Gtk::Entry>();
        sbox->append(*m_model_entry);

        m_url_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        auto lu = Gtk::make_managed<Gtk::Label>("Address (like https://example.com/v1)");
        lu->set_xalign(0);
        m_url_box->append(*lu);
        m_url_entry = Gtk::make_managed<Gtk::Entry>();
        m_url_box->append(*m_url_entry);
        sbox->append(*m_url_box);

        m_service->set_child(*sbox);
    }
    box->append(*m_service);

    m_provider->property_selected().signal_changed().connect([this] {
        if (m_loading)
            return;
        store_fields(); // what was typed for the one we leave
        const auto n = m_provider->get_selected();
        if (n < ai_providers().size())
            m_settings.provider = ai_providers().at(n).id;
        load_provider_fields();
    });

    auto bbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    bbox->set_halign(Gtk::Align::CENTER);
    m_spinner = Gtk::make_managed<Gtk::Spinner>();
    bbox->append(*m_spinner);
    m_make_button = Gtk::make_managed<Gtk::Button>("Make it");
    m_make_button->add_css_class("suggested-action");
    m_make_button->signal_clicked().connect([this] { start(); });
    bbox->append(*m_make_button);
    box->append(*bbox);

    // no key: any chatbot will do
    auto sep = Gtk::make_managed<Gtk::Separator>();
    box->append(*sep);
    auto chat = Gtk::make_managed<Gtk::Label>(
            "No key? Use any chatbot (ChatGPT, Gemini, Claude...): copy the prompt, paste it there, copy the "
            "answer and paste it back here.");
    chat->set_wrap(true);
    chat->set_xalign(0);
    chat->add_css_class("dim-label");
    box->append(*chat);
    auto cbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    cbox->set_halign(Gtk::Align::CENTER);
    auto copy = Gtk::make_managed<Gtk::Button>("Copy prompt for a chatbot");
    copy->signal_clicked().connect([this] { copy_prompt(); });
    cbox->append(*copy);
    auto paste = Gtk::make_managed<Gtk::Button>("Paste the answer");
    paste->signal_clicked().connect([this] { paste_answer(); });
    cbox->append(*paste);
    box->append(*cbox);

    m_status = Gtk::make_managed<Gtk::Label>();
    m_status->set_wrap(true);
    m_status->add_css_class("dim-label");
    box->append(*m_status);

    m_dispatch_done.connect([this] { finish(); });
    load_provider_fields();
    m_service->set_expanded(m_settings.get_key().empty());
}

DescribeWindow::~DescribeWindow()
{
    m_closed = true;
    store_fields();
    // an answer can take a while; don't wait for it
    if (m_thread.joinable())
        m_thread.detach();
}

void DescribeWindow::load_provider_fields()
{
    m_loading = true;
    const auto &providers = ai_providers();
    for (size_t i = 0; i < providers.size(); i++) {
        if (providers.at(i).id == ai_provider(m_settings.provider).id)
            m_provider->set_selected(i);
    }
    const auto &p = ai_provider(m_settings.provider);
    m_settings.provider = p.id;
    const auto key = m_settings.keys.find(p.id);
    m_key_entry->set_text(key != m_settings.keys.end() ? key->second : "");
    m_model_entry->set_text(m_settings.get_model());
    m_model_entry->set_placeholder_text(p.model);
    m_url_entry->set_text(m_settings.get_url());
    m_url_box->set_visible(p.url.empty());
    m_get_key->set_visible(p.key_page.size());
    m_loading = false;
}

// the fields' contents into the settings, for the service shown
void DescribeWindow::store_fields()
{
    const auto &p = ai_provider(m_settings.provider);
    const auto set = [&](std::map<std::string, std::string> &m, const std::string &v, const std::string &def) {
        if (v.empty() || v == def)
            m.erase(p.id);
        else
            m[p.id] = v;
    };
    set(m_settings.keys, m_key_entry->get_text(), "");
    set(m_settings.models, m_model_entry->get_text(), p.model);
    set(m_settings.urls, m_url_entry->get_text(), p.url);
}

std::string DescribeWindow::get_description()
{
    return m_text->get_buffer()->get_text();
}

void DescribeWindow::set_description(const std::string &text)
{
    m_text->get_buffer()->set_text(text);
}

void DescribeWindow::start()
{
    if (m_thread.joinable())
        return;
    store_fields();
    const auto &p = ai_provider(m_settings.provider);
    if (m_settings.get_key().empty() && p.id != "other") {
        m_service->set_expanded(true);
        m_status->set_text("This needs an API key - paste it in the field above (the button next to it shows where "
                           "to get one), or use the chatbot buttons below");
        return;
    }
    m_description = get_description();
    if (m_description.find_first_not_of(" \t\r\n") == std::string::npos) {
        m_status->set_text("Write what to make first");
        return;
    }
    m_make_button->set_sensitive(false);
    m_spinner->start();
    m_status->set_text("The AI is working on it... (this can take a minute)");
    m_thread = std::thread([this, settings = m_settings, description = m_description] {
        auto answer = ask_for_recipe(settings, description);
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
        if (m_error.starts_with("The API key wasn't accepted"))
            m_service->set_expanded(true); // ask again
        return;
    }
    if (m_on_recipe(m_recipe, m_description))
        m_status->set_text("Done - it's in the drawing. Describe something else, or close this.");
    else
        m_status->set_text("The recipe couldn't be built - try describing it differently");
}

void DescribeWindow::copy_prompt()
{
    const auto description = get_description();
    if (description.find_first_not_of(" \t\r\n") == std::string::npos) {
        m_status->set_text("Write what to make first");
        return;
    }
    get_clipboard()->set_text(make_chatbot_prompt(description));
    m_status->set_text("Copied. Paste it into a chatbot, then copy its whole answer and press \"Paste the answer\".");
}

void DescribeWindow::paste_answer()
{
    m_description = get_description();
    auto clipboard = get_clipboard();
    clipboard->read_text_async([this, clipboard](const Glib::RefPtr<Gio::AsyncResult> &result) {
        Glib::ustring text;
        try {
            text = clipboard->read_text_finish(result);
        }
        catch (const Glib::Error &) {
        }
        const auto answer = parse_recipe_text(text);
        if (answer.error.size()) {
            m_status->set_text(text.empty() ? "The clipboard is empty - copy the chatbot's answer first" : answer.error);
            return;
        }
        if (m_on_recipe(answer.recipe, m_description.size() ? m_description : "from a chatbot"))
            m_status->set_text("Done - it's in the drawing. Describe something else, or close this.");
        else
            m_status->set_text("The recipe couldn't be built - ask the chatbot to try again");
    });
}

} // namespace dune3d

#include "render_window.hpp"
#include "util/fs_util.hpp"
#include "util/gtk_util.hpp"
#include "util/uuid.hpp"
#include <array>

namespace dune3d {

namespace {
struct SizePreset {
    const char *label;
    int width, height; // 0: as the view, 1920 wide
};
const std::array<SizePreset, 5> size_presets = {{
        {"Like the view", 0, 0},
        {"Full HD (1920 x 1080)", 1920, 1080},
        {"4K (3840 x 2160)", 3840, 2160},
        {"Square (1080 x 1080)", 1080, 1080},
        {"Portrait (1080 x 1350)", 1080, 1350},
}};
struct QualityPreset {
    const char *label;
    int samples;
};
const std::array<QualityPreset, 3> quality_presets = {{
        {"Quick look", 24},
        {"Good", 128},
        {"Best (slow)", 512},
}};
struct BackgroundPreset {
    const char *label;
    std::optional<std::array<uint8_t, 3>> color; // none: transparent
};
const std::array<BackgroundPreset, 4> background_presets = {{
        {"White", std::array<uint8_t, 3>{255, 255, 255}},
        {"Light grey", std::array<uint8_t, 3>{226, 228, 232}},
        {"Dark", std::array<uint8_t, 3>{38, 40, 46}},
        {"Transparent (PNG)", std::nullopt},
}};

Gtk::DropDown *make_dropdown(const std::vector<const char *> &labels, guint selected)
{
    auto items = Gtk::StringList::create();
    for (auto l : labels)
        items->append(l);
    auto dd = Gtk::make_managed<Gtk::DropDown>(items);
    dd->set_selected(selected);
    dd->set_hexpand(true);
    return dd;
}
} // namespace

RenderWindow::RenderWindow(Gtk::Window &parent, std::filesystem::path &blender_path, double view_aspect,
                           ExportScene export_scene)
    : m_blender_path(blender_path), m_view_aspect(view_aspect), m_export_scene(std::move(export_scene))
{
    set_title("Render picture");
    add_css_class("studio-dialog");
    set_transient_for(parent);
    set_default_size(760, 640);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    box->set_margin(14);
    set_child(*box);

    auto grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(6);
    grid->set_column_spacing(8);
    grid->add_css_class("object-editor");
    int top = 0;
    {
        std::vector<const char *> labels;
        for (const auto &p : size_presets)
            labels.push_back(p.label);
        m_size = make_dropdown(labels, 1);
        grid_attach_label_and_widget(*grid, "Size", *m_size, top);
    }
    {
        std::vector<const char *> labels;
        for (const auto &p : quality_presets)
            labels.push_back(p.label);
        m_quality = make_dropdown(labels, 1);
        grid_attach_label_and_widget(*grid, "Quality", *m_quality, top);
    }
    {
        std::vector<const char *> labels;
        for (const auto &p : background_presets)
            labels.push_back(p.label);
        m_background = make_dropdown(labels, 0);
        m_background->property_selected().signal_changed().connect([this] { show_result(); });
        grid_attach_label_and_widget(*grid, "Background", *m_background, top);
    }
    {
        m_floor = Gtk::make_managed<Gtk::Switch>();
        m_floor->set_active(true);
        m_floor->set_halign(Gtk::Align::START);
        m_floor->set_tooltip_text("Soft shadows on the ground under the objects");
        grid_attach_label_and_widget(*grid, "Floor shadow", *m_floor, top);
    }
    {
        m_fill = Gtk::make_managed<Gtk::Switch>();
        m_fill->set_active(true);
        m_fill->set_halign(Gtk::Align::START);
        m_fill->set_tooltip_text("Look from the same direction as now, close enough for the objects to fill the "
                                 "picture; off: exactly as the view shows them");
        grid_attach_label_and_widget(*grid, "Fill the picture", *m_fill, top);
    }
    {
        auto bbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        m_blender_label = Gtk::make_managed<Gtk::Label>();
        m_blender_label->set_hexpand(true);
        m_blender_label->set_xalign(0);
        m_blender_label->set_ellipsize(Pango::EllipsizeMode::START);
        bbox->append(*m_blender_label);
        auto choose = Gtk::make_managed<Gtk::Button>("Choose...");
        choose->set_tooltip_text("Where Blender's blender.exe is");
        choose->signal_clicked().connect([this] { choose_blender(); });
        bbox->append(*choose);
        m_get_blender = Gtk::make_managed<Gtk::Button>("Get Blender (free)");
        m_get_blender->signal_clicked().connect([this] {
            Gtk::UriLauncher::create("https://www.blender.org/download/")->launch(*this, [](auto &) {});
        });
        bbox->append(*m_get_blender);
        grid_attach_label_and_widget(*grid, "Blender", *bbox, top);
    }
    box->append(*grid);

    {
        auto bbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        bbox->set_halign(Gtk::Align::CENTER);
        m_render_button = Gtk::make_managed<Gtk::Button>("Render");
        m_render_button->add_css_class("suggested-action");
        m_render_button->signal_clicked().connect([this] { start_render(); });
        bbox->append(*m_render_button);
        m_cancel_button = Gtk::make_managed<Gtk::Button>("Stop");
        m_cancel_button->set_sensitive(false);
        m_cancel_button->signal_clicked().connect([this] { m_render.cancel(); });
        bbox->append(*m_cancel_button);
        m_save_button = Gtk::make_managed<Gtk::Button>("Save picture...");
        m_save_button->set_sensitive(false);
        m_save_button->signal_clicked().connect([this] { save_picture(); });
        bbox->append(*m_save_button);
        box->append(*bbox);
    }
    m_progress = Gtk::make_managed<Gtk::ProgressBar>();
    box->append(*m_progress);
    m_status = Gtk::make_managed<Gtk::Label>();
    m_status->set_wrap(true);
    m_status->add_css_class("dim-label");
    box->append(*m_status);

    m_picture = Gtk::make_managed<Gtk::Picture>();
    m_picture->set_vexpand(true);
    m_picture->set_can_shrink(true);
    m_picture->set_content_fit(Gtk::ContentFit::CONTAIN);
    box->append(*m_picture);

    m_dispatch_progress.connect([this] {
        if (m_total > 0) {
            m_progress->set_fraction(static_cast<double>(m_done) / m_total);
            m_status->set_text("Rendering... " + std::to_string(m_done) + " of " + std::to_string(m_total));
        }
    });
    m_dispatch_done.connect([this] { finish(); });

    signal_close_request().connect(
            [this] {
                if (is_rendering())
                    m_render.cancel();
                return false;
            },
            false);

    update_blender();
}

RenderWindow::~RenderWindow()
{
    m_render.cancel();
    if (m_thread.joinable())
        m_thread.join();
    if (!m_dir.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(m_dir, ec);
    }
}

void RenderWindow::update_blender()
{
    const auto blender = find_blender(m_blender_path);
    if (blender) {
        m_blender_label->set_text(path_to_string(*blender));
        m_get_blender->set_visible(false);
    }
    else {
        m_blender_label->set_text("Not found - it's free, and needed for rendering");
        m_get_blender->set_visible(true);
    }
    m_render_button->set_sensitive(blender.has_value() && !is_rendering());
}

void RenderWindow::choose_blender()
{
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Where's blender.exe?");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("Blender");
    filter->add_pattern("blender.exe");
    filter->add_pattern("blender");
    filters->append(filter);
    dialog->set_filters(filters);
    dialog->open(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto file = dialog->open_finish(result);
            m_blender_path = path_from_string(file->get_path());
            update_blender();
        }
        catch (const Glib::Error &) {
        }
    });
}

std::pair<int, int> RenderWindow::get_size() const
{
    const auto &p = size_presets.at(m_size->get_selected());
    if (p.width)
        return {p.width, p.height};
    const double aspect = m_view_aspect > 0 ? m_view_aspect : 16. / 9.;
    return {1920, std::max(1, static_cast<int>(std::lround(1920 / aspect)))};
}

void RenderWindow::start_render()
{
    if (is_rendering())
        return;
    const auto blender = find_blender(m_blender_path);
    if (!blender)
        return;
    if (m_dir.empty()) {
        m_dir = std::filesystem::path(path_from_string(Glib::get_tmp_dir()))
                / ("tavnit3d-render-" + static_cast<std::string>(UUID::random()));
        std::filesystem::create_directories(m_dir);
    }
    const auto glb = m_dir / "scene.glb";
    m_png = m_dir / "render.png";
    std::error_code ec;
    std::filesystem::remove(m_png, ec);
    const auto [w, h] = get_size();
    if (!m_export_scene(glb, static_cast<double>(w) / h)) {
        m_status->set_text("There's nothing to render - add an object first");
        return;
    }
    BlenderRenderSettings settings;
    settings.width = w;
    settings.height = h;
    settings.samples = quality_presets.at(m_quality->get_selected()).samples;
    settings.floor_shadow = m_floor->get_active();
    settings.fill = m_fill->get_active();

    m_done = 0;
    m_total = 0;
    m_progress->set_fraction(0);
    m_status->set_text("Starting Blender...");
    m_render_button->set_sensitive(false);
    m_cancel_button->set_sensitive(true);
    m_thread = std::thread([this, b = *blender, glb, settings] {
        m_error = m_render.run(b, glb, m_png, settings, [this](int done, int total) {
            m_done = done;
            m_total = total;
            m_dispatch_progress.emit();
        });
        m_dispatch_done.emit();
    });
}

void RenderWindow::finish()
{
    if (m_thread.joinable())
        m_thread.join();
    m_cancel_button->set_sensitive(false);
    update_blender();
    if (m_error.size()) {
        m_status->set_text(m_error);
        m_progress->set_fraction(0);
        return;
    }
    try {
        m_rendered = Gdk::Texture::create_from_filename(path_to_string(m_png));
    }
    catch (const Glib::Error &e) {
        m_status->set_text(std::string("Couldn't load the picture: ") + e.what());
        return;
    }
    m_progress->set_fraction(1);
    const auto [w, h] = get_size();
    m_status->set_text("Done: " + std::to_string(w) + " x " + std::to_string(h));
    show_result();
}

// the rendered picture (transparent background) on the chosen background
void RenderWindow::show_result()
{
    if (!m_rendered)
        return;
    const auto &bg = background_presets.at(m_background->get_selected()).color;
    if (!bg) {
        m_result = m_rendered;
    }
    else {
        const int w = m_rendered->get_width(), h = m_rendered->get_height();
        const size_t stride = static_cast<size_t>(w) * 4;
        std::vector<guchar> px(stride * h);
        m_rendered->download(px.data(), stride); // premultiplied BGRA
        for (size_t i = 0; i < px.size(); i += 4) {
            const unsigned a = px[i + 3];
            px[i + 0] = px[i + 0] + (bg->at(2) * (255 - a) + 127) / 255;
            px[i + 1] = px[i + 1] + (bg->at(1) * (255 - a) + 127) / 255;
            px[i + 2] = px[i + 2] + (bg->at(0) * (255 - a) + 127) / 255;
            px[i + 3] = 255;
        }
        auto bytes = Glib::Bytes::create(px.data(), px.size());
        m_result = Gdk::MemoryTexture::create(w, h, Gdk::MemoryTexture::Format::B8G8R8A8_PREMULTIPLIED, bytes, stride);
    }
    m_picture->set_paintable(m_result);
    m_save_button->set_sensitive(true);
}

void RenderWindow::save_picture()
{
    if (!m_result)
        return;
    auto dialog = Gtk::FileDialog::create();
    dialog->set_initial_name("render.png");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("PNG picture");
    filter->add_pattern("*.png");
    filters->append(filter);
    dialog->set_filters(filters);
    dialog->save(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto file = dialog->save_finish(result);
            auto path = file->get_path();
            if (!path.ends_with(".png"))
                path += ".png";
            if (!m_result->save_to_png(path))
                m_status->set_text("Couldn't save the picture");
            else
                m_status->set_text("Saved " + path);
        }
        catch (const Glib::Error &) {
        }
    });
}

} // namespace dune3d

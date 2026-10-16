#include "editor.hpp"
#include "dune3d_appwindow.hpp"
#include "workspace_browser.hpp"
#include "canvas/canvas.hpp"
#include "document/document.hpp"
#include "document/decal.hpp"
#include "document/primitives.hpp"
#include "document/group/group.hpp"
#include "util/color.hpp"
#include "document/face_sketch.hpp"
#include "document/body_solids.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include "util/picture_util.hpp"
#include "util/picture_data.hpp"
#include "util/fs_util.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace dune3d {

/*
 * Stickers: pick a picture, then click on the object where it should go.
 */

void Editor::choose_picture_file(const std::string &title, std::function<void(const std::filesystem::path &)> cb)
{
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title(title);
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("Pictures");
    for (const auto pat : {"*.png", "*.jpg", "*.jpeg", "*.gif", "*.bmp", "*.webp", "*.tif", "*.tiff"})
        filter->add_pattern(pat);
    filters->append(filter);
    dialog->set_filters(filters);
    dialog->open(m_win, [this, dialog, cb](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto file = dialog->open_finish(result);
            cb(path_from_string(file->get_path()));
        }
        catch (const Gtk::DialogError &err) {
            // cancelled
        }
        catch (const Glib::Error &err) {
            m_workspace_browser->show_toast("Can't open that picture: " + std::string(err.what()));
        }
    });
}

void Editor::choose_decal_picture(const UUID &doc_uu, const UUID &body_uu)
{
    choose_picture_file("Picture for the sticker",
                        [this, doc_uu, body_uu](const std::filesystem::path &picture) {
                            begin_place_decal(doc_uu, body_uu, picture);
                        });
}

/*
 * A picture as an object of its own: a 1 mm slab with the picture as a sticker
 * on its top, as wide as the picture's proportions allow. It moves, resizes
 * (width, depth, height in the object panel) and stretches like any box.
 */
void Editor::add_picture_object()
{
    if (m_core.tool_is_active() || !m_core.has_documents())
        return;
    choose_picture_file("Picture to add", [this](const std::filesystem::path &picture) {
        std::shared_ptr<const PictureData> data;
        try {
            data = picture_data_from_file(picture);
        }
        catch (const Glib::Error &err) {
            m_workspace_browser->show_toast("Can't open that picture: " + std::string(err.what()));
            return;
        }
        if (!data || !data->m_width || !data->m_height || m_core.tool_is_active())
            return;
        CanvasUpdater canvas_updater{*this};
        auto &doc = m_core.get_current_document();
        PrimitiveParams params;
        params.width = 60;
        params.depth = std::clamp(std::round(params.width * data->m_height / data->m_width * 10) / 10, 1., 1000.);
        params.height = 1;
        const bool first = get_body_solids(doc, m_core.get_current_group()).empty();
        const auto pos = find_free_spot(doc, m_core.get_current_group(), params.width);
        auto &group = dune3d::add_primitive(doc, m_core.get_current_group(), Primitive::BOX, pos, params);
        const auto body = group.find_body(doc).group.m_uuid;
        {
            auto &b = *doc.get_group(body).m_body;
            const auto stem = path_to_string(picture.stem());
            b.m_name = stem.empty() ? "Picture" : stem;
            b.m_color = Color(1, 1, 1); // so the picture's own colours show
            b.m_material = Body::Material::MATTE;
        }
        m_core.set_needs_save();
        m_core.rebuild("add picture");

        // the sticker goes on the slab's big face: the one across its thinnest direction
        const auto solids = get_body_solids(doc, UUID(), [&body](const UUID &b) { return b == body; });
        if (solids.size()) {
            const auto &faces = solids.front().solid->m_faces;
            glm::dvec3 lo{1e300}, hi{-1e300};
            for (const auto &face : faces) {
                for (const auto &v : face.vertices) {
                    lo = glm::min(lo, glm::dvec3(v.x, v.y, v.z));
                    hi = glm::max(hi, glm::dvec3(v.x, v.y, v.z));
                }
            }
            const auto ext = hi - lo;
            int thin = 0;
            for (int i = 1; i < 3; i++) {
                if (ext[i] < ext[thin])
                    thin = i;
            }
            Decal decal;
            decal.m_data_uuid = data->m_uuid;
            decal.m_data = data;
            decal.m_origin = (lo + hi) / 2.;
            decal.m_origin[thin] = hi[thin];
            decal.m_normal = {0, 0, 0};
            decal.m_normal[thin] = 1;
            decal.m_anchor = get_bounds_center(faces);
            decal.m_width = params.width;
            doc.get_group(body).m_body->m_decals.push_back(decal);
            m_core.rebuild("add picture");
        }
        m_workspace_browser->update_documents(get_current_document_views());
        m_workspace_browser->select_group(group.m_uuid);
        zoom_to_fit(!first);
    });
}

void Editor::begin_place_decal(const UUID &doc_uu, const UUID &body_uu, const std::filesystem::path &picture)
{
    std::shared_ptr<const PictureData> data;
    try {
        data = picture_data_from_file(picture);
    }
    catch (const Glib::Error &err) {
        m_workspace_browser->show_toast("Can't open that picture: " + std::string(err.what()));
        return;
    }
    if (!data || !data->m_width || !data->m_height)
        return;
    m_placing_decal = PlacingDecal{.doc = doc_uu, .body = body_uu, .data = data};
    m_workspace_browser->show_toast("Click on the object where the sticker goes");
}

bool Editor::place_decal_under_cursor()
{
    if (!m_placing_decal)
        return false;
    const auto placing = *m_placing_decal;
    m_placing_decal.reset();
    if (!m_core.has_documents() || m_core.get_current_idocument_info().get_uuid() != placing.doc)
        return true;
    auto &doc = m_core.get_current_document();
    if (!doc.get_groups().contains(placing.body))
        return true;

    const auto [origin, dir] = get_canvas().get_cursor_ray();
    const auto only_this = [&placing](const UUID &b) { return b == placing.body; };
    const auto hit = pick_surface_point(doc, m_core.get_current_group(), origin, dir, only_this);
    const auto solids = get_body_solids(doc, m_core.get_current_group(), only_this);
    if (!hit || solids.empty()) {
        m_workspace_browser->show_toast("That's not on the object - no sticker added");
        return true;
    }
    const auto &faces = solids.front().solid->m_faces;

    Decal decal;
    decal.m_data_uuid = placing.data->m_uuid;
    decal.m_data = placing.data;
    decal.m_origin = hit->point;
    decal.m_normal = hit->normal;
    decal.m_anchor = get_bounds_center(faces);
    {
        // about half the object's size, in round millimetres
        glm::dvec3 lo{1e300}, hi{-1e300};
        for (const auto &face : faces) {
            for (const auto &v : face.vertices) {
                lo = glm::min(lo, glm::dvec3(v.x, v.y, v.z));
                hi = glm::max(hi, glm::dvec3(v.x, v.y, v.z));
            }
        }
        const auto ext = hi - lo;
        const double size = std::max({ext.x, ext.y, ext.z});
        decal.m_width = std::max(1., std::round(size * .5));
    }
    doc.get_group(placing.body).m_body->m_decals.push_back(decal);
    m_core.set_needs_save();
    m_core.rebuild("add sticker");
    canvas_update_keep_selection();
    show_object_editor(placing.doc, placing.body);
    return true;
}

} // namespace dune3d

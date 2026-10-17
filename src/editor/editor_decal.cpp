#include "editor.hpp"
#include "dune3d_appwindow.hpp"
#include "workspace_browser.hpp"
#include "canvas/canvas.hpp"
#include "document/document.hpp"
#include "document/decal.hpp"
#include "document/pattern.hpp"
#include "document/face_sketch.hpp"
#include "document/body_solids.hpp"
#include "document/solid_model/solid_model_occ.hpp"
#include "util/picture_util.hpp"
#include "util/picture_data.hpp"
#include "util/fs_util.hpp"
#include <cmath>
#include <iostream>

namespace dune3d {

/*
 * Stickers: pick a picture, then click on the object where it should go.
 */

void Editor::choose_decal_picture(const UUID &doc_uu, const UUID &body_uu)
{
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Picture for the sticker");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("Pictures");
    for (const auto pat : {"*.png", "*.jpg", "*.jpeg", "*.gif", "*.bmp", "*.webp", "*.tif", "*.tiff"})
        filter->add_pattern(pat);
    filters->append(filter);
    dialog->set_filters(filters);
    dialog->open(m_win, [this, dialog, doc_uu, body_uu](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto file = dialog->open_finish(result);
            begin_place_decal(doc_uu, body_uu, path_from_string(file->get_path()));
        }
        catch (const Gtk::DialogError &err) {
            // cancelled
        }
        catch (const Glib::Error &err) {
            m_workspace_browser->show_toast("Can't open that picture: " + std::string(err.what()));
        }
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

void Editor::begin_place_pattern(const UUID &doc_uu, const UUID &body_uu, int pattern, int colour)
{
    const auto &patterns = get_all_patterns();
    const auto &colours = get_all_pattern_colours();
    if (pattern < 0 || pattern >= (int)patterns.size() || colour < 0 || colour >= (int)colours.size())
        return;
    m_placing_decal = PlacingDecal{.doc = doc_uu,
                                   .body = body_uu,
                                   .data = make_pattern_picture(patterns.at(pattern).pattern, colours.at(colour)),
                                   .repeat = true};
    m_workspace_browser->show_toast("Click on the object: the pattern covers the faces that look that way");
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
    decal.m_repeat = placing.repeat;
    if (placing.repeat) {
        decal.m_width = 10; // one tile, in millimetres
    }
    else {
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

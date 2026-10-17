#include "solid_model.hpp"
#include "document/group/igroup_solid_model.hpp"
#include "util/color.hpp"
#include "document/group/group.hpp"
#include <TopoDS.hxx>

namespace dune3d {

class SolidModelOcc : public SolidModel {
public:
    TopoDS_Shape m_shape;
    TopoDS_Shape m_shape_acc;
    Color m_color;
    // faces with a colour of their own (by TShape), as imported
    std::map<const TopoDS_TShape *, Color> m_face_colors;
    // the body's material, and faces with another one (parts joined from objects of other materials)
    Body::Material m_material = Body::Material::SATIN;
    std::map<const TopoDS_TShape *, Body::Material> m_face_materials;


    void export_stl(const std::filesystem::path &path) const override;
    void add_to_step_exporter(STEPExporter &exporter, const char *name) const override;

    bool update_acc_finish(const Document &doc, const Group &group);
    void finish(const Document &doc, const Group &group);

    static TopoDS_Shape calc(IGroupSolidModel::Operation op, TopoDS_Shape argument, TopoDS_Shape tool);

private:
    void update_acc(IGroupSolidModel::Operation op, const TopoDS_Shape &last);
    void update_acc(IGroupSolidModel::Operation op, const SolidModelOcc *last);

    void triangulate();
    void find_edges();
};

} // namespace dune3d

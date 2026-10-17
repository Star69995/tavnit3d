#include "solid_model_occ.hpp"
#include "preferences/preferences.hpp"
#include "canvas/color_palette.hpp"
#include "document/group/group.hpp"
#include "document/group/igroup_solid_model.hpp"
#include "util/fs_util.hpp"
#include "util/step_exporter.hpp"

#include <Standard_Version.hxx>

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>

#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <Precision.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <ShapeAnalysis_Edge.hxx>
#include <BRepAdaptor_Curve.hxx>

#include <TDF_ChildIterator.hxx>
#include <Poly.hxx>
#include <glm/glm.hpp>
#include <map>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <StlAPI_Writer.hxx>


#include <GCPnts_TangentialDeflection.hxx>

#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>


namespace dune3d {

class Triangulator {
public:
    Triangulator(const TopoDS_Shape &shape, const Color &color, face::Faces &faces,
                 const std::map<const TopoDS_TShape *, Color> &face_colors,
                 const std::map<const TopoDS_TShape *, Body::Material> &face_materials);


private:
    bool processNode(const TopoDS_Shape &shape);
    bool processComp(const TopoDS_Shape &shape, const glm::dmat4 &mat_in = glm::dmat4(1));
    bool processSolid(const TopoDS_Shape &shape, const glm::dmat4 &mat_in = glm::dmat4(1));
    bool processShell(const TopoDS_Shape &shape, const glm::dmat4 &mat = glm::dmat4(1));
    bool processFace(const TopoDS_Face &face, const glm::dmat4 &mat = glm::dmat4(1));
    void add_mesh(const Poly_Triangulation &tri, const glm::dmat4 &mat, bool reversed, const face::Color &color);

    face::Faces &m_faces;
    face::Color m_color;
    const std::map<const TopoDS_TShape *, Color> &m_face_colors;
    const std::map<const TopoDS_TShape *, Body::Material> &m_face_materials;
    unsigned char get_material(const TopoDS_Face &face) const
    {
        if (auto it = m_face_materials.find(face.TShape().get()); it != m_face_materials.end())
            return static_cast<unsigned char>(it->second);
        return face::Face::no_material;
    }
    face::Color get_color(const TopoDS_Face &face) const
    {
        if (auto it = m_face_colors.find(face.TShape().get()); it != m_face_colors.end())
            return face::Color(it->second.r, it->second.g, it->second.b);
        return m_color;
    }
};

Triangulator::Triangulator(const TopoDS_Shape &shape, const Color &color, face::Faces &faces,
                           const std::map<const TopoDS_TShape *, Color> &face_colors,
                           const std::map<const TopoDS_TShape *, Body::Material> &face_materials)
    : m_faces(faces), m_face_colors(face_colors), m_face_materials(face_materials)
{
    m_color.r = color.r;
    m_color.b = color.b;
    m_color.g = color.g;
    processNode(shape);
}

#if (OCC_VERSION_MAJOR >= 7 && OCC_VERSION_MINOR >= 6) || (OCC_VERSION_MAJOR >= 8)
#define HORIZON_NEW_OCC
#endif

#define USER_PREC (0.14)
#define USER_ANGLE (0.52359878)

// the whole transformation, including any scaling (imported meshes have that)
static glm::dmat4 update_matrix(const gp_Trsf &tr, const glm::dmat4 &mat_in)
{
    glm::dmat4 m(1);
    for (int row = 1; row <= 3; row++) {
        for (int col = 1; col <= 4; col++)
            m[col - 1][row - 1] = tr.Value(row, col);
    }
    return mat_in * m;
}


bool Triangulator::processFace(const TopoDS_Face &face, const glm::dmat4 &mat_in)
{
    if (true == face.IsNull())
        return false;

    auto mat = update_matrix(face.Location().Transformation(), mat_in);

    TopLoc_Location loc;
    bool isTessellate(false);
    Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, loc);

    if (triangulation.IsNull() || triangulation->Deflection() > USER_PREC + Precision::Confusion())
        isTessellate = true;
    // an imported mesh is all there is: there's no surface to make a finer one from
    if (!triangulation.IsNull() && BRep_Tool::Surface(face).IsNull())
        isTessellate = false;

    if (isTessellate) {
        BRepMesh_IncrementalMesh IM(face, USER_PREC, false, USER_ANGLE);
        triangulation = BRep_Tool::Triangulation(face, loc);
    }

    if (triangulation.IsNull() == true)
        return false;

    if (BRep_Tool::Surface(face).IsNull()) {
        add_mesh(*triangulation, mat, face.Orientation() == TopAbs_REVERSED, get_color(face));
        m_faces.back().material = get_material(face);
        return true;
    }

    Poly::ComputeNormals(triangulation);

#ifndef HORIZON_NEW_OCC
    const TColgp_Array1OfPnt &arrPolyNodes = triangulation->Nodes();
    const Poly_Array1OfTriangle &arrTriangles = triangulation->Triangles();
    const TShort_Array1OfShortReal &arrNormals = triangulation->Normals();
#endif


    m_faces.emplace_back();
    auto &face_out = m_faces.back();
    face_out.color = get_color(face);
    face_out.material = get_material(face);
    face_out.vertices.reserve(triangulation->NbNodes());

    std::map<face::Vertex, std::vector<size_t>> pts_map;
    for (int i = 1; i <= triangulation->NbNodes(); i++) {
#ifdef HORIZON_NEW_OCC
        gp_XYZ v(triangulation->Node(i).Coord());
#else
        gp_XYZ v(arrPolyNodes(i).Coord());
#endif
        const glm::vec4 vg(v.X(), v.Y(), v.Z(), 1);
        const auto vt = mat * vg;
        const face::Vertex vertex(vt.x, vt.y, vt.z);
        pts_map[vertex].push_back(i - 1);
        face_out.vertices.push_back(vertex);
    }

    // the triangulation follows the surface; a reversed face points the other way
    const bool reversed = face.Orientation() == TopAbs_REVERSED;
    face_out.normals.reserve(triangulation->NbNodes());
    for (int i = 1; i <= triangulation->NbNodes(); i++) {
#ifdef HORIZON_NEW_OCC
        const auto n = triangulation->Normal(i);
        glm::vec4 vg(n.X(), n.Y(), n.Z(), 0);
#else
        auto offset = (i - 1) * 3 + 1;
        auto x = arrNormals(offset + 0);
        auto y = arrNormals(offset + 1);
        auto z = arrNormals(offset + 2);
        glm::vec4 vg(x, y, z, 0);
#endif
        auto vt = mat * vg;
        vt = glm::normalize(vt);
        if (reversed)
            vt = -vt;
        face_out.normals.emplace_back(vt.x, vt.y, vt.z);
    }

    // average normals at coincident vertices
    for (const auto &[k, v] : pts_map) {
        if (v.size() > 1) {
            face::Vertex n_acc(0, 0, 0);
            for (const auto idx : v) {
                n_acc += face_out.normals.at(idx);
            }
            n_acc /= v.size();
            for (const auto idx : v) {
                face_out.normals.at(idx) = n_acc;
            }
        }
    }

    face_out.triangle_indices.reserve(triangulation->NbTriangles());
    for (int i = 1; i <= triangulation->NbTriangles(); i++) {
        int a, b, c;
#ifdef HORIZON_NEW_OCC
        triangulation->Triangle(i).Get(a, b, c);
#else
        arrTriangles(i).Get(a, b, c);
#endif
        if (reversed)
            std::swap(b, c);
        face_out.triangle_indices.emplace_back(a - 1, b - 1, c - 1);
        // std::cout << "tr " << a - 1 << " " << b - 1 << " " << c - 1 << std::endl;
    }

    return true;
}

// An imported mesh (no surface underneath): its own normals if it has them,
// else smooth ones that keep edges sharper than about 40 degrees.
void Triangulator::add_mesh(const Poly_Triangulation &tri, const glm::dmat4 &mat, bool reversed,
                            const face::Color &color)
{
    auto &face_out = m_faces.emplace_back();
    face_out.color = color;
    const int n_nodes = tri.NbNodes();
    std::vector<glm::dvec3> pos(n_nodes);
    for (int i = 1; i <= n_nodes; i++) {
        const auto p = tri.Node(i);
        pos.at(i - 1) = glm::dvec3(mat * glm::dvec4(p.X(), p.Y(), p.Z(), 1));
    }
    std::vector<std::array<int, 3>> tris;
    tris.reserve(tri.NbTriangles());
    for (int i = 1; i <= tri.NbTriangles(); i++) {
        int a, b, c;
        tri.Triangle(i).Get(a, b, c);
        if (reversed)
            std::swap(b, c);
        tris.push_back({a - 1, b - 1, c - 1});
    }

    if (tri.HasNormals()) {
        for (int i = 1; i <= n_nodes; i++) {
            const auto &p = pos.at(i - 1);
            face_out.vertices.emplace_back(p.x, p.y, p.z);
            const auto nn = tri.Normal(i);
            auto n = glm::dvec3(mat * glm::dvec4(nn.X(), nn.Y(), nn.Z(), 0));
            if (glm::length(n) > 0)
                n = glm::normalize(n);
            if (reversed)
                n = -n;
            face_out.normals.emplace_back(n.x, n.y, n.z);
        }
        for (const auto &[a, b, c] : tris)
            face_out.triangle_indices.emplace_back(a, b, c);
        return;
    }

    std::vector<glm::dvec3> tri_n; // area weighted
    std::vector<std::vector<size_t>> node_tris(n_nodes);
    for (size_t t = 0; t < tris.size(); t++) {
        const auto &[a, b, c] = tris.at(t);
        tri_n.push_back(glm::cross(pos.at(b) - pos.at(a), pos.at(c) - pos.at(a)));
        for (const auto i : tris.at(t))
            node_tris.at(i).push_back(t);
    }
    const double cos_crease = std::cos(glm::radians(40.));
    std::map<std::pair<int, std::array<int, 3>>, size_t> out_index;
    for (size_t t = 0; t < tris.size(); t++) {
        const auto len = glm::length(tri_n.at(t));
        const auto own = len > 0 ? tri_n.at(t) / len : glm::dvec3(0, 0, 1);
        std::array<size_t, 3> idx;
        for (int k = 0; k < 3; k++) {
            const int node = tris.at(t).at(k);
            glm::dvec3 n{0, 0, 0};
            for (const auto s : node_tris.at(node)) {
                const auto ls = glm::length(tri_n.at(s));
                if (ls > 0 && glm::dot(tri_n.at(s) / ls, own) >= cos_crease)
                    n += tri_n.at(s);
            }
            n = glm::length(n) > 0 ? glm::normalize(n) : own;
            const std::array<int, 3> q = {(int)std::lround(n.x * 1000), (int)std::lround(n.y * 1000),
                                          (int)std::lround(n.z * 1000)};
            auto [it, inserted] = out_index.emplace(std::make_pair(node, q), face_out.vertices.size());
            if (inserted) {
                const auto &p = pos.at(node);
                face_out.vertices.emplace_back(p.x, p.y, p.z);
                face_out.normals.emplace_back(n.x, n.y, n.z);
            }
            idx.at(k) = it->second;
        }
        face_out.triangle_indices.emplace_back(idx.at(0), idx.at(1), idx.at(2));
    }
}

bool Triangulator::processShell(const TopoDS_Shape &shape, const glm::dmat4 &mat)
{
    TopoDS_Iterator it;
    bool ret = false;
    for (it.Initialize(shape, true, false); it.More(); it.Next()) {
        const TopoDS_Face &face = TopoDS::Face(it.Value());

        if (processFace(face, mat))
            ret = true;
    }

    return ret;
}

bool Triangulator::processSolid(const TopoDS_Shape &shape, const glm::dmat4 &mat_in)
{
    bool ret = false;

    auto mat = update_matrix(shape.Location().Transformation(), mat_in);

    TopoDS_Iterator it;
    for (it.Initialize(shape, true, false); it.More(); it.Next()) {
        const TopoDS_Shape &subShape = it.Value();

        if (processShell(subShape, mat))
            ret = true;
    }

    return ret;
}


bool Triangulator::processComp(const TopoDS_Shape &shape, const glm::dmat4 &mat_in)
{
    TopoDS_Iterator it;

    auto mat = update_matrix(shape.Location().Transformation(), mat_in);

    bool ret = false;

    for (it.Initialize(shape, true, false); it.More(); it.Next()) {
        const TopoDS_Shape &subShape = it.Value();
        TopAbs_ShapeEnum stype = subShape.ShapeType();

        switch (stype) {
        case TopAbs_COMPOUND:
        case TopAbs_COMPSOLID:
            if (processComp(subShape, mat))
                ret = true;
            break;

        case TopAbs_SOLID:
            if (processSolid(subShape, mat))
                ret = true;
            break;

        case TopAbs_SHELL:
            if (processShell(subShape))
                ret = true;
            break;

        case TopAbs_FACE:
            if (processFace(TopoDS::Face(subShape)))
                ret = true;
            break;

        default:
            break;
        }
    }

    return ret;
}

bool Triangulator::processNode(const TopoDS_Shape &shape)
{
    TopAbs_ShapeEnum stype = shape.ShapeType();
    bool ret = true;
    switch (stype) {
    case TopAbs_COMPOUND:
    case TopAbs_COMPSOLID:
        if (processComp(shape))
            ret = true;
        break;

    case TopAbs_SOLID:
        if (processSolid(shape))
            ret = true;
        break;

    case TopAbs_SHELL:
        if (processShell(shape))
            ret = true;
        break;

    case TopAbs_FACE:
        if (processFace(TopoDS::Face(shape)))
            ret = true;
        break;

    default:
        break;
    }

    return ret;
}


void SolidModelOcc::triangulate()
{
    m_faces.clear();
    Triangulator tri{m_shape_acc, m_color, m_faces, m_face_colors, m_face_materials};
}

inline double defaultAngularDeflection(double linearTolerance)
{
    // Default OCC angular deflection is 0.5 radians, or about 28.6 degrees.
    // That is a bit coarser than necessary for performance, so we default to at
    // most 0.1 radians, or 5.7 degrees. We also do not go finer than 0.005, or
    // roughly 0.28 degree angular resolution, to avoid performance tanking
    // completely at very fine resolutions.
    return std::min(0.1, linearTolerance * 5 + 0.005);
}

void SolidModelOcc::export_stl(const std::filesystem::path &path) const
{
    StlAPI_Writer writer;
    double deflection = 0.001;
#if OCC_VERSION_HEX < 0x060801
    if (deflection > 0) {
        writer.RelativeMode() = false;
        writer.SetDeflection(deflection);
    }
#else
    TopoDS_Shape sh = m_shape_acc;
    BRepMesh_IncrementalMesh aMesh(m_shape_acc, deflection,
                                   /*isRelative*/ false,
                                   /*theAngDeflection*/
                                   0.5,
                                   /*isInParallel*/ true);
#endif
    writer.Write(m_shape_acc, path_to_string(path).c_str());
}

void SolidModelOcc::add_to_step_exporter(STEPExporter &exporter, const char *name) const
{
    std::vector<std::pair<TopoDS_Shape, Color>> face_colors;
    for (TopExp_Explorer ex(m_shape_acc, TopAbs_FACE); ex.More(); ex.Next()) {
        if (auto it = m_face_colors.find(ex.Current().TShape().get()); it != m_face_colors.end())
            face_colors.emplace_back(ex.Current(), it->second);
    }
    exporter.add_model(name, m_shape_acc, m_color, face_colors);
}

void SolidModelOcc::find_edges()
{
    m_edges.clear();
    TopExp_Explorer topex(m_shape_acc, TopAbs_EDGE);
    std::list<TopoDS_Shape> edges;
    unsigned int edge_idx = 0;
    while (topex.More()) {
        auto edge = TopoDS::Edge(topex.Current());
        bool skip = false;
        for (const auto &other : edges) {
            if (other.IsSame(topex.Current())) {
                skip = true;
                break;
            }
        }
        if (skip) {
            topex.Next();
            edge_idx++;
            continue;
        }
        edges.push_back(edge);
        {
            auto curve = BRepAdaptor_Curve(edge);
            GCPnts_TangentialDeflection discretizer(curve, M_PI / 16, 1e3);
            auto &e = m_edges[edge_idx];
            if (discretizer.NbPoints() > 0) {
                int nbPoints = discretizer.NbPoints();
                for (int i = 1; i <= nbPoints; i++) {
                    const gp_Pnt pnt = discretizer.Value(i);
                    e.emplace_back(pnt.X(), pnt.Y(), pnt.Z());
                }
            }
        }
        topex.Next();
        edge_idx++;
    }
}

TopoDS_Shape SolidModelOcc::calc(IGroupSolidModel::Operation op, TopoDS_Shape argument, TopoDS_Shape tool)
{
    switch (op) {
    case IGroupSolidModel::Operation::DIFFERENCE:
        return BRepAlgoAPI_Cut(argument, tool);

    case IGroupSolidModel::Operation::UNION:
        return BRepAlgoAPI_Fuse(argument, tool);

    case IGroupSolidModel::Operation::INTERSECTION:
        return BRepAlgoAPI_Common(argument, tool);

    default:
        throw std::runtime_error("unsuppored solid model operation");
    }
}

void SolidModelOcc::update_acc(IGroupSolidModel::Operation op, const TopoDS_Shape &last)
{
    m_shape_acc = calc(op, last, m_shape);
}

void SolidModelOcc::update_acc(IGroupSolidModel::Operation op, const SolidModelOcc *last)
{
    if (last)
        update_acc(op, last->m_shape_acc);
    else
        m_shape_acc = m_shape;
}


bool SolidModelOcc::update_acc_finish(const Document &doc, const Group &group)
{
    auto operation = dynamic_cast<const IGroupSolidModel &>(group).get_operation();
    const auto last_solid_model = dynamic_cast<const SolidModelOcc *>(get_last_solid_model(doc, group));
    update_acc(operation, last_solid_model);

    if (m_shape_acc.IsNull()) {
        return false;
    }

    finish(doc, group);

    return true;
}

void SolidModelOcc::finish(const Document &doc, const Group &group)
{
    auto body = group.find_body(doc).body;
    if (body.m_color) {
        m_color = *body.m_color;
    }
    else {
        m_color = Preferences::get().canvas.appearance.get_color(ColorP::SOLID_MODEL);
    }

    m_material = body.m_material;

    triangulate();
    find_edges();
}

} // namespace dune3d

#include <gdkmm/texture.h> // before OCCT, whose windows.h defines IGNORE
#include <glibmm/bytes.h>
#include "step_importer.hpp"
#include <Quantity_Color.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFApp_Application.hxx>
#include <Standard_Version.hxx>

#include <AIS_Shape.hxx>

#include <IGESCAFControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <Interface_Static.hxx>

#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Reader.hxx>

#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

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
#include <Quantity_Color.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <ShapeAnalysis_Edge.hxx>
#include <BRepAdaptor_Curve.hxx>

#include <TDF_ChildIterator.hxx>
#include <Poly.hxx>
#include <gp_Circ.hxx>
#include <GCPnts_TangentialDeflection.hxx>

#define USER_PREC (0.14)
#define USER_ANGLE (0.52359878)

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/transform.hpp>
#include <map>
#include <list>

#include "util/fs_util.hpp"
#include "shapes.hpp"
#include <RWGltf_CafReader.hxx>
#include <RWObj_CafReader.hxx>
#include <XCAFDoc_VisMaterial.hxx>
#include <TDataStd_Name.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Builder.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Image_Texture.hxx>
#include <fstream>
#include <Bnd_Box.hxx>
#include <Message_ProgressRange.hxx>
#include <algorithm>

namespace dune3d::STEPImporter {

// adapted from
// https://github.com/KiCad/kicad-source-mirror/blob/master/plugins/3d/oce/loadmodel.cpp


bool STEPImporter::readSTEP(const char *fname)
{
    STEPCAFControl_Reader reader;
    IFSelect_ReturnStatus stat = reader.ReadFile(fname);

    if (stat != IFSelect_RetDone)
        return false;

    // Enable user-defined shape precision
    if (!Interface_Static::SetIVal("read.precision.mode", 1))
        return false;

    // Set the shape conversion precision to USER_PREC (default 0.0001 has too
    // many triangles)
    if (!Interface_Static::SetRVal("read.precision.val", USER_PREC))
        return false;

    // set other translation options
    reader.SetColorMode(true);  // use model colors
    reader.SetNameMode(true);   // names for the imported objects
    reader.SetLayerMode(false); // ignore LAYER data

    if (!reader.Transfer(m_doc)) {
        m_doc->Close();
        return false;
    }

    // are there any shapes to translate?
    if (reader.NbRootsForTransfer() < 1)
        return false;

    return true;
}


bool STEPImporter::readIGES(const char *fname)
{
    IGESCAFControl_Reader reader;
    if (reader.ReadFile(fname) != IFSelect_RetDone)
        return false;
    reader.SetColorMode(true);
    reader.SetNameMode(true);
    reader.SetLayerMode(false);
    if (!reader.Transfer(m_doc)) {
        m_doc->Close();
        return false;
    }
    return true;
}

// glTF (metres, y up) or OBJ (no units; taken as millimetres unless it's tiny,
// then metres - y up like most programs write it)
bool STEPImporter::readMesh(const std::filesystem::path &filename, bool gltf)
{
    auto read = [this, &filename, gltf](double file_unit) {
        std::unique_ptr<RWMesh_CafReader> reader;
        if (gltf) {
            auto r = std::make_unique<RWGltf_CafReader>();
            r->SetParallel(true);
            reader = std::move(r);
        }
        else {
            reader = std::make_unique<RWObj_CafReader>();
        }
        reader->SetSystemLengthUnit(0.001);
        reader->SetFileLengthUnit(file_unit);
        reader->SetSystemCoordinateSystem(RWMesh_CoordinateSystem_Zup);
        reader->SetFileCoordinateSystem(RWMesh_CoordinateSystem_glTF);
        reader->SetDocument(m_doc);
        return reader->Perform(TCollection_AsciiString(path_to_string(filename).c_str()), Message_ProgressRange());
    };
    // glTF triangles may be left in the file to load later; get them now
    auto load_deferred = [this] {
        NCollection_Sequence<TDF_Label> labels;
        XCAFDoc_DocumentTool::ShapeTool(m_doc->Main())->GetFreeShapes(labels);
        BRep_Builder builder;
        for (const auto &label : labels) {
            for (TopExp_Explorer ex(XCAFDoc_ShapeTool::GetShape(label), TopAbs_FACE); ex.More(); ex.Next()) {
                const auto &face = TopoDS::Face(ex.Current());
                TopLoc_Location loc;
                auto tri = BRep_Tool::Triangulation(face, loc);
                if (!tri.IsNull() && tri->HasDeferredData()) {
                    auto loaded = tri->DetachedLoadDeferredData();
                    if (!loaded.IsNull())
                        builder.UpdateFace(face, loaded);
                }
            }
        }
    };
    if (gltf) {
        if (!read(1))
            return false;
        load_deferred();
        return true;
    }
    if (!read(0.001))
        return false;
    Bnd_Box box;
    NCollection_Sequence<TDF_Label> labels;
    XCAFDoc_DocumentTool::ShapeTool(m_doc->Main())->GetFreeShapes(labels);
    for (const auto &label : labels)
        BRepBndLib::Add(XCAFDoc_ShapeTool::GetShape(label), box, false);
    if (box.IsVoid())
        return true;
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    if (std::max({x1 - x0, y1 - y0, z1 - z0}) >= 10)
        return true;
    // smaller than a centimetre in millimetres: much more likely metres
    m_doc->Close();
    m_app->NewDocument("MDTV-XCAF", m_doc);
    return read(1);
}

STEPImporter::STEPImporter(const std::filesystem::path &filename)
{
    m_app = XCAFApp_Application::GetApplication();

    m_app->NewDocument("MDTV-XCAF", m_doc);
    auto ext = path_to_string(filename.extension());
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return std::tolower(c); });
    bool ok;
    if (ext == ".igs" || ext == ".iges")
        ok = readIGES(path_to_string(filename).c_str());
    else if (ext == ".gltf" || ext == ".glb" || ext == ".obj")
        ok = readMesh(filename, ext != ".obj");
    else
        ok = readSTEP(path_to_string(filename).c_str());
    if (!ok) {
        std::cout << "error loading " << filename << std::endl;
        loaded = false;
        return;
    }
    std::cout << "loaded" << std::endl;
    loaded = true;

    m_assy = XCAFDoc_DocumentTool::ShapeTool(m_doc->Main());
    m_color = XCAFDoc_DocumentTool::ColorTool(m_doc->Main());
    m_vis = XCAFDoc_DocumentTool::VisMaterialTool(m_doc->Main());
}

void STEPImporter::processWire(const TopoDS_Wire &wire, const glm::dmat4 &mat)
{
    for (BRepTools_WireExplorer expl(wire); expl.More(); expl.Next()) {
        const auto &edge = expl.Current();
        ShapeAnalysis_Edge sh_an;
        auto first = sh_an.FirstVertex(edge);
        {
            gp_Pnt pnt = BRep_Tool::Pnt(first);
            glm::dvec4 gpnt(pnt.X(), pnt.Y(), pnt.Z(), 1);
            auto pt = mat * gpnt;
            result->points.emplace_back(pt.x, pt.y, pt.z);
        }

        auto curve = BRepAdaptor_Curve(edge);
        const auto curvetype = curve.GetType();
        if (curvetype == GeomAbs_Circle) {
            gp_Circ c = curve.Circle();
            auto pnt = c.Location();
            {
                glm::dvec4 gpnt(pnt.X(), pnt.Y(), pnt.Z(), 1);
                auto pt = mat * gpnt;
                result->points.emplace_back(pt.x, pt.y, pt.z);
            }
        }
    }
}

#if (OCC_VERSION_MAJOR >= 7 && OCC_VERSION_MINOR >= 6) || (OCC_VERSION_MAJOR >= 8)
#define HORIZON_NEW_OCC
#endif

bool STEPImporter::processFace(const TopoDS_Face &face, Quantity_Color *color, const glm::dmat4 &mat)
{
    if (true == face.IsNull())
        return false;

    {
        TopoDS_Iterator it;
        for (it.Initialize(face, false, false); it.More(); it.Next()) {
            const TopoDS_Shape &subShape = it.Value();
            TopAbs_ShapeEnum stype = subShape.ShapeType();
            if (stype == TopAbs_WIRE) {
                const TopoDS_Wire &wire = TopoDS::Wire(it.Value());
                processWire(wire, mat);
            }
        }
    }

    // bool reverse = ( face.Orientation() == TopAbs_REVERSED );

    //	bool useBothSides = false;

    TopLoc_Location loc;
    bool isTessellate(false);
    Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, loc);

    if (triangulation.IsNull() || triangulation->Deflection() > USER_PREC + Precision::Confusion())
        isTessellate = true;

    if (isTessellate) {
        BRepMesh_IncrementalMesh IM(face, USER_PREC, false, USER_ANGLE);
        triangulation = BRep_Tool::Triangulation(face, loc);
    }

    if (triangulation.IsNull() == true)
        return false;

    Quantity_Color lcolor;

    // check for a face color; this has precedence over SOLID colors
    do {
        TDF_Label L;

        if (m_color->ShapeTool()->Search(face, L)) {
            if (m_color->GetColor(L, XCAFDoc_ColorGen, lcolor) || m_color->GetColor(L, XCAFDoc_ColorCurv, lcolor)
                || m_color->GetColor(L, XCAFDoc_ColorSurf, lcolor))
                color = &lcolor;
        }
    } while (0);

    Poly::ComputeNormals(triangulation);

#ifndef HORIZON_NEW_OCC
    const TColgp_Array1OfPnt &arrPolyNodes = triangulation->Nodes();
    const Poly_Array1OfTriangle &arrTriangles = triangulation->Triangles();
    const TShort_Array1OfShortReal &arrNormals = triangulation->Normals();
#endif


    result->faces.emplace_back();
    auto &face_out = result->faces.back();
    if (color) {
        face_out.color.r = color->Red();
        face_out.color.g = color->Green();
        face_out.color.b = color->Blue();
    }
    else {
        face_out.color = {0.5, 0.5, 0.5};
    }
    face_out.vertices.reserve(triangulation->NbNodes());

    std::map<Vertex, std::vector<size_t>> pts_map;
    for (int i = 1; i <= triangulation->NbNodes(); i++) {
#ifdef HORIZON_NEW_OCC
        gp_XYZ v(triangulation->Node(i).Coord());
#else
        gp_XYZ v(arrPolyNodes(i).Coord());
#endif
        const glm::vec4 vg(v.X(), v.Y(), v.Z(), 1);
        const auto vt = mat * vg;
        const Vertex vertex(vt.x, vt.y, vt.z);
        pts_map[vertex].push_back(i - 1);
        face_out.vertices.push_back(vertex);
    }

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
        vt /= vt.length();
        face_out.normals.emplace_back(vt.x, vt.y, vt.z);
    }

    // average normals at coincident vertices
    for (const auto &[k, v] : pts_map) {
        if (v.size() > 1) {
            Vertex n_acc(0, 0, 0);
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
        face_out.triangle_indices.emplace_back(a - 1, b - 1, c - 1);
    }

    return true;
}

bool STEPImporter::processShell(const TopoDS_Shape &shape, Quantity_Color *color, const glm::dmat4 &mat)
{
    TopoDS_Iterator it;
    bool ret = false;

    for (it.Initialize(shape, false, false); it.More(); it.Next()) {
        const TopoDS_Face &face = TopoDS::Face(it.Value());

        if (processFace(face, color, mat))
            ret = true;
    }

    return ret;
}

bool STEPImporter::getColor(TDF_Label label, Quantity_Color &color)
{
    while (true) {
        if (m_color->GetColor(label, XCAFDoc_ColorGen, color))
            return true;
        else if (m_color->GetColor(label, XCAFDoc_ColorSurf, color))
            return true;
        else if (m_color->GetColor(label, XCAFDoc_ColorCurv, color))
            return true;

        label = label.Father();

        if (label.IsNull())
            break;
    };

    return false;
}

bool STEPImporter::processSolid(const TopoDS_Shape &shape, const glm::dmat4 &mat_in)
{
    TDF_Label label = m_assy->FindShape(shape, false);
    bool ret = false;

    hasSolid = true;

    Quantity_Color col;
    Quantity_Color *lcolor = NULL;

    if (label.IsNull()) {
    }
    else {
        if (getColor(label, col))
            lcolor = &col;
    }

    TopLoc_Location loc = shape.Location();
    gp_Trsf T = loc.Transformation();
    gp_XYZ coord = T.TranslationPart();

    auto mat = mat_in * glm::translate(glm::dvec3(coord.X(), coord.Y(), coord.Z()));

    gp_XYZ axis;
    double angle;

    if (T.GetRotation(axis, angle)) {
        glm::dvec3 gaxis(axis.X(), axis.Y(), axis.Z());
        double angle_f = angle;
        mat = glm::rotate(mat, angle_f, gaxis);
    }

    TopoDS_Iterator it;
    for (it.Initialize(shape, false, false); it.More(); it.Next()) {
        const TopoDS_Shape &subShape = it.Value();

        if (processShell(subShape, lcolor, mat))
            ret = true;
    }

    return ret;
}


bool STEPImporter::processComp(const TopoDS_Shape &shape, const glm::dmat4 &mat_in)
{
    TopoDS_Iterator it;
    TopLoc_Location loc = shape.Location();
    gp_Trsf T = loc.Transformation();
    gp_XYZ coord = T.TranslationPart();

    auto mat = mat_in * glm::translate(glm::dvec3(coord.X(), coord.Y(), coord.Z()));

    gp_XYZ axis;
    double angle;

    if (T.GetRotation(axis, angle)) {
        glm::dvec3 gaxis(axis.X(), axis.Y(), axis.Z());
        double angle_f = angle;
        mat = glm::rotate(mat, angle_f, gaxis);
    }

    bool ret = false;

    for (it.Initialize(shape, false, false); it.More(); it.Next()) {
        const TopoDS_Shape &subShape = it.Value();
        TopAbs_ShapeEnum stype = subShape.ShapeType();

        hasSolid = false;

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
            if (processShell(subShape, NULL, mat))
                ret = true;
            break;

        case TopAbs_FACE:
            if (processFace(TopoDS::Face(subShape), NULL, mat))
                ret = true;
            break;

        default:
            break;
        }
    }

    return ret;
}

bool STEPImporter::processNode(const TopoDS_Shape &shape)
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
        if (processShell(shape, NULL))
            ret = true;
        break;

    case TopAbs_FACE:
        if (processFace(TopoDS::Face(shape), NULL))
            ret = true;
        break;

    default:
        break;
    }

    return ret;
}

Result STEPImporter::get_faces_and_points()
{
    Result res;
    result = &res;

    NCollection_Sequence<TDF_Label> frshapes;
    m_assy->GetFreeShapes(frshapes);

    int nshapes = frshapes.Length();
    int id = 1;
    std::cout << "shapes " << nshapes << std::endl;
    while (id <= nshapes) {
        TopoDS_Shape shape = m_assy->GetShape(frshapes.Value(id));
        if (!shape.IsNull() && processNode(shape)) {
            TopExp_Explorer topex(shape, TopAbs_EDGE);
            std::list<TopoDS_Shape> edges;
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
                    continue;
                }
                edges.push_back(edge);
                {
                    auto curve = BRepAdaptor_Curve(edge);
                    GCPnts_TangentialDeflection discretizer(curve, M_PI / 16, 1e3);
                    auto &e = res.edges.emplace_back();
                    if (discretizer.NbPoints() > 0) {
                        int nbPoints = discretizer.NbPoints();
                        for (int i = 1; i <= nbPoints; i++) {
                            const gp_Pnt pnt = discretizer.Value(i);
                            e.emplace_back(pnt.X(), pnt.Y(), pnt.Z());
                        }
                    }
                }
                topex.Next();
            }
        }
        ++id;
    }

    result = nullptr;
    return res;
}

std::vector<TopoDS_Shape> STEPImporter::get_shapes()
{
    std::vector<TopoDS_Shape> r;
    NCollection_Sequence<TDF_Label> frshapes;
    m_assy->GetFreeShapes(frshapes);

    int nshapes = frshapes.Length();
    int id = 1;
    std::cout << "shapes " << nshapes << std::endl;
    while (id <= nshapes) {
        TopoDS_Shape shape = m_assy->GetShape(frshapes.Value(id));
        if (!shape.IsNull()) {
            r.push_back(shape);
        }
        ++id;
    }
    return r;
}

static std::array<float, 3> to_srgb(const Quantity_Color &c)
{
    double r, g, b;
    c.Values(r, g, b, Quantity_TOC_sRGB);
    return {static_cast<float>(r), static_cast<float>(g), static_cast<float>(b)};
}

// What colour a texture is overall, for a material that takes its colour from one
// (we have no textures, just one colour per object).
static std::optional<std::array<float, 3>> get_average_color(const Handle(Image_Texture) & tex)
{
    if (tex.IsNull())
        return {};
    std::string bytes;
    try {
        if (!tex->DataBuffer().IsNull()) {
            bytes.assign(reinterpret_cast<const char *>(tex->DataBuffer()->Data()), tex->DataBuffer()->Size());
        }
        else if (!tex->FilePath().IsEmpty()) {
            std::ifstream ifs(path_from_string(tex->FilePath().ToCString()), std::ios::binary);
            if (!ifs)
                return {};
            if (tex->FileOffset() > 0)
                ifs.seekg(tex->FileOffset());
            if (tex->FileLength() > 0) {
                bytes.resize(tex->FileLength());
                ifs.read(bytes.data(), bytes.size());
            }
            else {
                bytes.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
            }
        }
        if (bytes.empty())
            return {};
        auto texture = Gdk::Texture::create_from_bytes(Glib::Bytes::create(bytes.data(), bytes.size()));
        const int w = texture->get_width(), h = texture->get_height();
        std::vector<guchar> px(static_cast<size_t>(w) * h * 4);
        texture->download(px.data(), w * 4);
        double sum[3] = {0, 0, 0};
        size_t n = 0;
        const int step = std::max(1, std::max(w, h) / 128);
        for (int y = 0; y < h; y += step) {
            for (int x = 0; x < w; x += step) {
                const auto p = px.data() + (static_cast<size_t>(y) * w + x) * 4; // BGRA
                if (p[3] < 128)
                    continue;
                sum[0] += p[2];
                sum[1] += p[1];
                sum[2] += p[0];
                n++;
            }
        }
        if (!n)
            return {};
        return std::array<float, 3>{static_cast<float>(sum[0] / n / 255), static_cast<float>(sum[1] / n / 255),
                                    static_cast<float>(sum[2] / n / 255)};
    }
    catch (...) {
        return {};
    }
}

static std::optional<STEPImporter::Appearance> appearance_from_material(const Handle(XCAFDoc_VisMaterial) & mat)
{
    if (mat.IsNull() || mat->IsEmpty())
        return {};
    STEPImporter::Appearance a;
    a.has_material = true;
    if (!mat->RawName().IsNull())
        a.name = mat->RawName()->ToCString();
    if (mat->HasPbrMaterial()) {
        const auto &pbr = mat->PbrMaterial();
        a.color = to_srgb(pbr.BaseColor.GetRGB());
        if (auto tc = get_average_color(pbr.BaseColorTexture)) {
            for (int i = 0; i < 3; i++)
                a.color->at(i) *= tc->at(i);
        }
        a.alpha = pbr.BaseColor.Alpha();
        a.metallic = pbr.Metallic;
        a.roughness = pbr.Roughness;
    }
    else {
        const auto &com = mat->CommonMaterial();
        a.color = to_srgb(com.DiffuseColor);
        if (auto tc = get_average_color(com.DiffuseTexture)) {
            for (int i = 0; i < 3; i++)
                a.color->at(i) *= tc->at(i);
        }
        a.alpha = 1 - com.Transparency;
        a.roughness = 1 - std::clamp(com.Shininess, 0.f, 1.f);
    }
    return a;
}

std::optional<STEPImporter::Appearance> STEPImporter::get_appearance(const TDF_Label &label)
{
    if (label.IsNull())
        return {};
    if (!m_vis.IsNull()) {
        if (auto a = appearance_from_material(m_vis->GetShapeMaterial(label)))
            return a;
    }
    Quantity_Color col;
    if (m_color->GetColor(label, XCAFDoc_ColorSurf, col) || m_color->GetColor(label, XCAFDoc_ColorGen, col)) {
        Appearance a;
        a.color = to_srgb(col);
        return a;
    }
    return {};
}

std::optional<STEPImporter::Appearance> STEPImporter::get_appearance(const TopoDS_Shape &shape)
{
    if (auto it = m_shape_appearance.find(shape.TShape().get()); it != m_shape_appearance.end())
        return it->second;
    if (!m_vis.IsNull()) {
        if (auto a = appearance_from_material(m_vis->GetShapeMaterial(shape)))
            return a;
    }
    Quantity_Color col;
    if (m_color->GetColor(shape, XCAFDoc_ColorSurf, col) || m_color->GetColor(shape, XCAFDoc_ColorGen, col)) {
        Appearance a;
        a.color = to_srgb(col);
        return a;
    }
    return {};
}

static std::string get_label_name(const TDF_Label &label)
{
    Handle(TDataStd_Name) name;
    if (!label.IsNull() && label.FindAttribute(TDataStd_Name::GetID(), name)) {
        TCollection_AsciiString s(name->Get()); // UTF-8, keeps Hebrew names
        return s.ToCString();
    }
    return {};
}

void STEPImporter::collect_parts(const TDF_Label &label, const TopLoc_Location &loc,
                                 std::optional<Appearance> inherited, std::vector<ImportPart> &parts)
{
    if (auto a = get_appearance(label))
        inherited = a;
    const auto label_name = get_label_name(label);
    if (inherited && inherited->name.empty())
        inherited->name = label_name;

    if (XCAFDoc_ShapeTool::IsAssembly(label)) {
        TDF_LabelSequence components;
        XCAFDoc_ShapeTool::GetComponents(label, components);
        for (const auto &comp : components) {
            TDF_Label ref;
            if (!XCAFDoc_ShapeTool::GetReferredShape(comp, ref))
                continue;
            auto app = inherited;
            if (auto a = get_appearance(comp))
                app = a; // the instance's own colour wins
            collect_parts(ref, loc * XCAFDoc_ShapeTool::GetLocation(comp), app, parts);
        }
        return;
    }

    const auto shape = XCAFDoc_ShapeTool::GetShape(label);
    if (shape.IsNull())
        return;

    auto add = [&parts, &label_name](const std::optional<Appearance> &app, const TopoDS_Shape &sh) -> ImportPart & {
        Appearance a = app.value_or(Appearance{});
        auto it = std::ranges::find_if(parts, [&a](const ImportPart &p) {
            Appearance pa;
            pa.color = p.color;
            pa.has_material = p.has_material;
            pa.metallic = p.metallic;
            pa.roughness = p.roughness;
            pa.alpha = p.alpha;
            return pa.key() == a.key();
        });
        if (it == parts.end()) {
            auto &p = parts.emplace_back();
            p.name = label_name.size() ? label_name : a.name; // material names tend to be "Material.001"
            p.color = a.color;
            p.has_material = a.has_material;
            p.metallic = a.metallic;
            p.roughness = a.roughness;
            p.alpha = a.alpha;
            it = parts.end() - 1;
        }
        it->shapes.push_back(sh);
        return *it;
    };

    // solids stay whole, in the colour most of their faces have
    bool any_solid = false;
    for (TopExp_Explorer ex(shape, TopAbs_SOLID); ex.More(); ex.Next()) {
        any_solid = true;
        auto app = get_appearance(ex.Current());
        if (!app) {
            // by its faces: the colour covering most of it, the others are kept per face
            std::map<std::tuple<std::array<int, 3>, bool, int, int, int>, std::pair<double, std::optional<Appearance>>>
                    areas;
            for (TopExp_Explorer fx(ex.Current(), TopAbs_FACE); fx.More(); fx.Next()) {
                GProp_GProps props;
                BRepGProp::SurfaceProperties(fx.Current(), props);
                const auto fa = get_appearance(fx.Current());
                auto &c = areas[fa.value_or(inherited.value_or(Appearance{})).key()];
                c.first += props.Mass();
                c.second = fa ? fa : inherited;
            }
            double best = -1;
            for (const auto &[k, v] : areas) {
                if (v.first > best) {
                    best = v.first;
                    app = v.second;
                }
            }
            if (!app)
                app = inherited;
            auto &part = add(app, ex.Current().Moved(loc));
            const auto main_key = app.value_or(Appearance{}).key();
            for (TopExp_Explorer fx(ex.Current(), TopAbs_FACE); fx.More(); fx.Next()) {
                const auto fa = get_appearance(fx.Current());
                if (fa && fa->color && fa->key() != main_key)
                    part.face_colors.emplace_back(fx.Current(), *fa->color);
            }
            continue;
        }
        add(app, ex.Current().Moved(loc));
    }
    if (any_solid)
        return;
    // meshes and loose faces, face by face
    for (TopExp_Explorer fx(shape, TopAbs_FACE); fx.More(); fx.Next()) {
        auto app = get_appearance(fx.Current());
        if (!app)
            app = inherited;
        add(app, fx.Current().Moved(loc));
    }
}

std::vector<ImportPart> STEPImporter::get_parts()
{
    std::vector<ImportPart> parts;
    NCollection_Sequence<TDF_Label> frshapes;
    m_assy->GetFreeShapes(frshapes);
    // what's coloured or has a material, by shape regardless of where it's placed
    m_shape_appearance.clear();
    TDF_LabelSequence labels;
    m_assy->GetShapes(labels);
    for (const auto &label : labels) {
        TDF_LabelSequence subs;
        XCAFDoc_ShapeTool::GetSubShapes(label, subs);
        subs.Prepend(label);
        for (const auto &l : subs) {
            if (auto app = get_appearance(l)) {
                const auto sh = XCAFDoc_ShapeTool::GetShape(l);
                if (!sh.IsNull())
                    m_shape_appearance.emplace(sh.TShape().get(), *app);
            }
        }
    }
    for (const auto &label : frshapes)
        collect_parts(label, TopLoc_Location(), {}, parts);
    for (size_t i = 0; i < parts.size(); i++) {
        if (parts.at(i).name.empty())
            parts.at(i).name = "Part " + std::to_string(i + 1);
    }
    return parts;
}

Result import(const std::filesystem::path &filename)
{
    STEPImporter importer(filename);
    if (!importer.is_loaded())
        return {};
    return importer.get_faces_and_points();
}

std::shared_ptr<const Shapes> import_shapes(const std::filesystem::path &filename)
{
    STEPImporter importer(filename);
    if (!importer.is_loaded())
        return {};
    auto shapes = std ::make_shared<Shapes>();
    shapes->shapes = importer.get_shapes();
    shapes->parts = importer.get_parts();
    return shapes;
}

Result::~Result() = default;


} // namespace dune3d::STEPImporter

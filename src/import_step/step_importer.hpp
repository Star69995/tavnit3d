#pragma once
#include "import.hpp"
#include <TDocStd_Document.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <Quantity_Color.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_VisMaterialTool.hxx>
#include <TopLoc_Location.hxx>
#include "shapes.hpp"
#include <optional>
#include <map>
#include <glm/glm.hpp>
#include <filesystem>

namespace dune3d::STEPImporter {
class STEPImporter {
public:
    STEPImporter(const std::filesystem::path &filename);

    Result get_faces_and_points();
    bool is_loaded() const
    {
        return loaded;
    }
    std::vector<TopoDS_Shape> get_shapes();
    std::vector<ImportPart> get_parts();
    struct Appearance {
        std::optional<std::array<float, 3>> color;
        bool has_material = false;
        float metallic = 0, roughness = .5, alpha = 1;
        std::string name;

        auto key() const
        {
            auto r = [](float v) { return static_cast<int>(std::round(v * 1000)); };
            std::array<int, 3> c = {-1, -1, -1};
            if (color)
                c = {r(color->at(0)), r(color->at(1)), r(color->at(2))};
            return std::make_tuple(c, has_material, r(metallic), r(roughness), r(alpha));
        }
    };

private:
    bool readSTEP(const char *fname);
    bool readIGES(const char *fname);
    bool readMesh(const std::filesystem::path &filename, bool gltf);
    bool processNode(const TopoDS_Shape &shape);
    bool processComp(const TopoDS_Shape &shape, const glm::dmat4 &mat_in = glm::dmat4(1));
    bool processSolid(const TopoDS_Shape &shape, const glm::dmat4 &mat_in = glm::dmat4(1));
    bool getColor(TDF_Label label, Quantity_Color &color);
    bool processShell(const TopoDS_Shape &shape, Quantity_Color *color, const glm::dmat4 &mat = glm::dmat4(1));
    bool processFace(const TopoDS_Face &face, Quantity_Color *color, const glm::dmat4 &mat = glm::dmat4(1));
    void processWire(const TopoDS_Wire &wire, const glm::dmat4 &mat);

    Handle(XCAFApp_Application) m_app;
    Handle(TDocStd_Document) m_doc;
    Handle(XCAFDoc_ColorTool) m_color;
    Handle(XCAFDoc_ShapeTool) m_assy;
    Handle(XCAFDoc_VisMaterialTool) m_vis;
    std::map<const TopoDS_TShape *, Appearance> m_shape_appearance;

    std::optional<Appearance> get_appearance(const TDF_Label &label);
    std::optional<Appearance> get_appearance(const TopoDS_Shape &shape);
    void collect_parts(const TDF_Label &label, const TopLoc_Location &loc, std::optional<Appearance> inherited,
                       std::vector<ImportPart> &parts);
    bool hasSolid;
    bool loaded = false;

    Result *result;
};
} // namespace dune3d::STEPImporter

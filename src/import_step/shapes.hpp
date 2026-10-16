#pragma once
#include <vector>
#include <string>
#include <optional>
#include <array>
#include <TopoDS_Shape.hxx>

namespace dune3d::STEPImporter {

// Everything in the file that looks the same (colour and material), so it
// can become one object. Solids stay whole; meshes (glTF, OBJ) are grouped
// face by face.
class ImportPart {
public:
    std::string name;
    std::optional<std::array<float, 3>> color; // sRGB
    // a physically based material, if the file has one
    bool has_material = false;
    float metallic = 0;
    float roughness = .5;
    float alpha = 1;
    std::vector<TopoDS_Shape> shapes; // placed where they are in the file
    // faces (of the above, by TShape) coloured differently from the rest
    std::vector<std::pair<TopoDS_Shape, std::array<float, 3>>> face_colors;
};

class Shapes {
public:
    std::vector<TopoDS_Shape> shapes;
    std::vector<ImportPart> parts;
};

} // namespace dune3d::STEPImporter

#pragma once
#include "color.hpp"
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

#include <TopoDS_Shape.hxx>

namespace dune3d {
class STEPExporter {
public:
    STEPExporter(const char *assy_name);
    ~STEPExporter();
    // faces with a colour of their own (the shape's own faces) override the colour of the whole
    void add_model(const char *name, const TopoDS_Shape &shape, const Color &color,
                   const std::vector<std::pair<TopoDS_Shape, Color>> &face_colors = {});
    void write(const std::filesystem::path &path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace dune3d

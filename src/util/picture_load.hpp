#pragma once
#include <filesystem>
#include <vector>
#include <cstdint>

namespace dune3d {

class Document;
class PictureData;

void pictures_load(Document &doc, const std::filesystem::path &dir);
void pictures_save(const Document &doc, const std::filesystem::path &dir);
std::vector<uint8_t> picture_data_to_png(const PictureData &data);

} // namespace dune3d

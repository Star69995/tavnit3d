// Imported objects keep their names, also in Hebrew (Shapr3D writes them as \X2\ in STEP).
#include "preferences/preferences.hpp" // before OCCT, whose windows.h breaks cairomm
#include "import_step/step_importer.hpp"
#include <BRepPrimAPI_MakeBox.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TDataStd_Name.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <glibmm/miscutils.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace dune3d;

const Preferences &Preferences::get()
{
    static Preferences the_preferences;
    return the_preferences;
}

static int failures = 0;

static void check(bool ok, const std::string &what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok)
        failures++;
}

int main()
{
    const std::string name = "\xd7\xa8\xd7\x92\xd7\x9c"; // "leg" in Hebrew, UTF-8
    const auto path = std::filesystem::path(Glib::get_tmp_dir()) / "dune3d-test-names.step";

    {
        Handle(TDocStd_Document) doc;
        XCAFApp_Application::GetApplication()->NewDocument("MDTV-XCAF", doc);
        auto shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        auto label = shapes->AddShape(BRepPrimAPI_MakeBox(10, 20, 30).Shape(), false);
        TDataStd_Name::Set(label, TCollection_ExtendedString(name.c_str(), true));
        STEPCAFControl_Writer writer;
        writer.SetNameMode(true);
        check(writer.Transfer(doc) && writer.Write(path.string().c_str()) == IFSelect_RetDone, "writes a STEP file");
    }
    {
        // OCCT writes the name as plain UTF-8; write it like Shapr3D does instead
        std::string text;
        {
            std::ifstream ifs(path, std::ios::binary);
            std::stringstream ss;
            ss << ifs.rdbuf();
            text = ss.str();
        }
        const std::string quoted = "'" + name + "'";
        size_t n = 0;
        for (auto pos = text.find(quoted); pos != std::string::npos; pos = text.find(quoted, pos), n++)
            text.replace(pos, quoted.size(), "'\\X2\\05E805D205DC\\X0\\'");
        check(n > 0, "the name is in the file");
        std::ofstream(path, std::ios::binary) << text;
    }

    STEPImporter::STEPImporter importer(path);
    check(importer.is_loaded(), "imports it");
    const auto parts = importer.get_parts();
    check(parts.size() == 1, "one object");
    if (parts.size())
        check(parts.front().name == name, "keeps the Hebrew name (got \"" + parts.front().name + "\")");

    std::filesystem::remove(path);
    std::cout << (failures ? "FAILED" : "all passed") << std::endl;
    return failures ? 1 : 0;
}

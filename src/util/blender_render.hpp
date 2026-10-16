#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

namespace dune3d {

// Photo-like pictures of a glTF scene (as from export_gltf) by running
// Blender's Cycles renderer in the background. The picture has a transparent
// background, so the caller can put it on any colour; a floor catches the
// objects' shadows.
struct BlenderRenderSettings {
    int width = 1920;
    int height = 1080;
    int samples = 128;
    bool floor_shadow = true;
    bool fill = true; // move the camera closer, so the objects fill the picture
};

// Blender's executable: the configured one if it's there, else from the usual
// install places (newest first) or PATH. DUNE3D_BLENDER overrides all of it.
std::optional<std::filesystem::path> find_blender(const std::filesystem::path &configured);

class BlenderRender {
public:
    // Blocks until done, so run it in a thread; progress(done, total) is called
    // from there. Returns an error message, empty if it worked.
    std::string run(const std::filesystem::path &blender, const std::filesystem::path &glb,
                    const std::filesystem::path &png, const BlenderRenderSettings &settings,
                    const std::function<void(int, int)> &progress);
    void cancel(); // from any thread

private:
    std::mutex m_mutex;
    void *m_process = nullptr;
    std::atomic_bool m_cancelled = false;
};

} // namespace dune3d

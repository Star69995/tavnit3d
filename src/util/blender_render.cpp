#include "blender_render.hpp"
#include "fs_util.hpp"
#include <glibmm/miscutils.h>
#include <fstream>
#include <regex>
#include <vector>
#include <algorithm>
#ifdef G_OS_WIN32
#include <windows.h>
#endif

namespace dune3d {

// Runs inside Blender: blender -b --factory-startup -P script.py -- in.glb out.png width height samples floor fill
static const char *render_script = R"PY(
import bpy, sys, math
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:]
glb, out = args[0], args[1]
width, height, samples = int(args[2]), int(args[3]), int(args[4])
floor = args[5] == "1"
fill = args[6] == "1"

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb)
scene = bpy.context.scene
meshes = [o for o in scene.objects if o.type == 'MESH']
cameras = [o for o in scene.objects if o.type == 'CAMERA']
lights = [o for o in scene.objects if o.type == 'LIGHT']

lo = Vector((1e9, 1e9, 1e9))
hi = Vector((-1e9, -1e9, -1e9))
for o in meshes:
    for c in o.bound_box:
        p = o.matrix_world @ Vector(c)
        lo = Vector(map(min, lo, p))
        hi = Vector(map(max, hi, p))
center = (lo + hi) / 2
size = max(max(hi - lo), 1e-3)

cam = cameras[0] if cameras else None
if cam is None:
    data = bpy.data.cameras.new("View")
    cam = bpy.data.objects.new("View", data)
    scene.collection.objects.link(cam)
    cam.location = center + Vector((1, -1, .8)).normalized() * size * 2.5
    cam.rotation_euler = (center - cam.location).to_track_quat('-Z', 'Y').to_euler()
scene.camera = cam
cam.data.clip_start = size * .001
cam.data.clip_end = size * 1000
if fill:
    # the same direction, but framing the objects' bounds with a little room around them
    m = cam.matrix_world.to_3x3()
    right, up, forward = m @ Vector((1, 0, 0)), m @ Vector((0, 1, 0)), m @ Vector((0, 0, -1))
    corners = [Vector((x, y, z)) - center for x in (lo.x, hi.x) for y in (lo.y, hi.y) for z in (lo.z, hi.z)]
    xs = [c.dot(right) for c in corners]
    ys = [c.dot(up) for c in corners]
    shift_x, shift_y = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
    aim = center + right * shift_x + up * shift_y
    xs = [x - shift_x for x in xs]
    ys = [y - shift_y for y in ys]
    zs = [-c.dot(forward) for c in corners]  # towards the camera
    aspect = width / height
    margin = 1.12
    cam.data.sensor_fit = 'AUTO'  # the importer may have set it to the height
    if cam.data.type == 'ORTHO':
        w, h = (max(xs) - min(xs)) * margin, (max(ys) - min(ys)) * margin
        # ortho_scale spans the longer side of the picture
        cam.data.ortho_scale = max(w, h * aspect) if aspect >= 1 else max(h, w / aspect)
        cam.location = aim - forward * size * 4
    else:
        t = math.tan(cam.data.angle / 2) / margin  # along the longer side
        tx, ty = (t, t / aspect) if aspect >= 1 else (t * aspect, t)
        d = max(max(abs(x) / tx + z for x, z in zip(xs, zs)), max(abs(y) / ty + z for y, z in zip(ys, zs)))
        cam.location = aim - forward * d

r = scene.render
r.engine = 'CYCLES'
r.resolution_x = width
r.resolution_y = height
r.resolution_percentage = 100
r.film_transparent = True
r.image_settings.file_format = 'PNG'
r.image_settings.color_mode = 'RGBA'
r.filepath = out
scene.cycles.samples = samples
scene.cycles.use_denoising = True
for vt in ('AgX', 'Filmic'):
    try:
        scene.view_settings.view_transform = vt
        break
    except TypeError:
        pass

# the graphics card if there's one Cycles can use
try:
    prefs = bpy.context.preferences.addons['cycles'].preferences
    for kind in ('OPTIX', 'CUDA', 'HIP', 'ONEAPI', 'METAL'):
        try:
            prefs.compute_device_type = kind
        except TypeError:
            continue
        prefs.get_devices()
        devices = [d for d in prefs.devices if d.type == kind]
        if devices:
            for d in prefs.devices:
                d.use = d.type == kind
            scene.cycles.device = 'GPU'
            print("RENDER-DEVICE", kind, flush=True)
            break
except Exception as e:
    print("no GPU:", e, flush=True)

# glass: real refraction rather than also being half transparent
for mat in bpy.data.materials:
    if not mat.use_nodes:
        continue
    for n in mat.node_tree.nodes:
        if n.type != 'BSDF_PRINCIPLED':
            continue
        t = n.inputs.get('Transmission Weight') or n.inputs.get('Transmission')
        if t is not None and t.default_value > .5:
            for link in list(n.inputs['Alpha'].links):
                mat.node_tree.links.remove(link)
            n.inputs['Alpha'].default_value = 1
            n.inputs['Roughness'].default_value = .02
            n.inputs['IOR'].default_value = 1.45

# soft surroundings, darker below and bright above, for shiny materials to reflect
world = bpy.data.worlds.new("Studio")
scene.world = world
world.use_nodes = True
nt = world.node_tree
bg = nt.nodes['Background']
coords = nt.nodes.new('ShaderNodeTexCoord')
sep = nt.nodes.new('ShaderNodeSeparateXYZ')
remap = nt.nodes.new('ShaderNodeMapRange')
remap.inputs['From Min'].default_value = -1
remap.inputs['From Max'].default_value = 1
ramp = nt.nodes.new('ShaderNodeValToRGB')
ramp.color_ramp.elements[0].position = .35
ramp.color_ramp.elements[0].color = (.22, .22, .24, 1)
ramp.color_ramp.elements[1].position = .75
ramp.color_ramp.elements[1].color = (1, 1, 1, 1)
nt.links.new(coords.outputs['Generated'], sep.inputs[0])
nt.links.new(sep.outputs['Z'], remap.inputs['Value'])
nt.links.new(remap.outputs['Result'], ramp.inputs['Fac'])
nt.links.new(ramp.outputs['Color'], bg.inputs['Color'])
bg.inputs['Strength'].default_value = .5 if lights else .4

def area_light(name, direction, irradiance, spread):
    d = size * 3
    data = bpy.data.lights.new(name, 'AREA')
    data.size = size * spread
    # a one-sided area light of power P gives about P / (pi d^2) straight ahead
    data.energy = irradiance * math.pi * d * d
    o = bpy.data.objects.new(name, data)
    scene.collection.objects.link(o)
    o.location = center + direction.normalized() * d
    o.rotation_euler = (center - o.location).to_track_quat('-Z', 'Y').to_euler()

if not lights:
    # a photo studio: key, fill and rim light, placed as seen from the camera
    m = cam.matrix_world.to_3x3()
    right, up, back = m @ Vector((1, 0, 0)), m @ Vector((0, 1, 0)), m @ Vector((0, 0, 1))
    area_light("Key", -.7 * right + .9 * up + .7 * back, 4, 1.5)
    area_light("Fill", .9 * right + .2 * up + .6 * back, 1.5, 2.5)
    area_light("Rim", .3 * right + .8 * up - 1.0 * back, 3, 1)

if floor:
    bpy.ops.mesh.primitive_plane_add(size=size * 60, location=(center.x, center.y, lo.z))
    plane = bpy.context.active_object
    plane.is_shadow_catcher = True

bpy.ops.render.render(write_still=True)
print("RENDER-DONE", flush=True)
)PY";

std::optional<std::filesystem::path> find_blender(const std::filesystem::path &configured)
{
    if (auto env = Glib::getenv("DUNE3D_BLENDER"); env.size())
        return path_from_string(env);
    if (!configured.empty() && std::filesystem::is_regular_file(configured))
        return configured;
#ifdef G_OS_WIN32
    std::vector<std::filesystem::path> found;
    for (const auto var : {"ProgramFiles", "ProgramW6432"}) {
        const auto pf = Glib::getenv(var);
        if (pf.empty())
            continue;
        const auto dir = path_from_string(pf) / "Blender Foundation";
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec))
            continue;
        for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
            const auto exe = entry.path() / "blender.exe";
            if (std::filesystem::is_regular_file(exe, ec))
                found.push_back(exe);
        }
    }
    if (const auto pf86 = Glib::getenv("ProgramFiles(x86)"); pf86.size()) {
        const auto steam = path_from_string(pf86) / "Steam" / "steamapps" / "common" / "Blender" / "blender.exe";
        std::error_code ec;
        if (std::filesystem::is_regular_file(steam, ec))
            found.push_back(steam);
    }
    if (found.size()) {
        // "Blender 4.2" sorts after "Blender 3.6"; good enough
        std::ranges::sort(found);
        return found.back();
    }
#endif
    if (auto p = Glib::find_program_in_path("blender"); p.size())
        return path_from_string(p);
    return {};
}

void BlenderRender::cancel()
{
    m_cancelled = true;
#ifdef G_OS_WIN32
    std::lock_guard lock{m_mutex};
    if (m_process)
        TerminateProcess(m_process, 1);
#endif
}

#ifdef G_OS_WIN32
static std::wstring quote(const std::wstring &s)
{
    return L"\"" + s + L"\"";
}
#endif

std::string BlenderRender::run(const std::filesystem::path &blender, const std::filesystem::path &glb,
                               const std::filesystem::path &png, const BlenderRenderSettings &settings,
                               const std::function<void(int, int)> &progress)
{
    m_cancelled = false;
    const auto script = std::filesystem::path(glb).replace_extension(".py");
    {
        std::ofstream ofs(script, std::ios::binary);
        ofs << render_script;
        if (!ofs)
            return "Couldn't write the render script";
    }
#ifdef G_OS_WIN32
    std::wstring cmd = quote(blender.wstring()) + L" -b --factory-startup -noaudio -P " + quote(script.wstring())
                       + L" -- " + quote(glb.wstring()) + L" " + quote(png.wstring()) + L" "
                       + std::to_wstring(settings.width) + L" " + std::to_wstring(settings.height) + L" "
                       + std::to_wstring(settings.samples) + L" " + (settings.floor_shadow ? L"1" : L"0") + L" "
                       + (settings.fill ? L"1" : L"0");

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE read_end = nullptr, write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &sa, 0))
        return "Couldn't start Blender";
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = write_end;
    si.hStdError = write_end;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    const bool ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                   &si, &pi);
    CloseHandle(write_end);
    if (!ok) {
        CloseHandle(read_end);
        return "Couldn't start Blender";
    }
    CloseHandle(pi.hThread);
    {
        std::lock_guard lock{m_mutex};
        m_process = pi.hProcess;
    }
    if (m_cancelled)
        TerminateProcess(pi.hProcess, 1);

    const std::regex sample_re{R"(Sample (\d+)/(\d+))"};
    std::string buf, log_tail;
    bool done = false;
    char chunk[4096];
    DWORD n = 0;
    while (ReadFile(read_end, chunk, sizeof(chunk), &n, nullptr) && n) {
        buf.append(chunk, n);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            const auto line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            std::smatch m;
            if (std::regex_search(line, m, sample_re) && progress)
                progress(std::stoi(m[1]), std::stoi(m[2]));
            if (line.find("RENDER-DONE") != std::string::npos)
                done = true;
            if (line.find("Error") != std::string::npos || line.find("Traceback") != std::string::npos)
                log_tail += line + "\n";
        }
    }
    CloseHandle(read_end);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    {
        std::lock_guard lock{m_mutex};
        m_process = nullptr;
    }
    CloseHandle(pi.hProcess);
    std::error_code ec;
    std::filesystem::remove(script, ec);
    if (m_cancelled)
        return "Cancelled";
    if (!done || code != 0 || !std::filesystem::is_regular_file(png))
        return "Blender couldn't render it" + (log_tail.size() ? ":\n" + log_tail : std::string("."));
    return {};
#else
    return "Rendering with Blender is only set up on Windows so far";
#endif
}

} // namespace dune3d

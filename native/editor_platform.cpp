// Editor platform services. Scene commands, history, tile tools and play mode live in Xenon.
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <raylib.h>
#include <raymath.h>
#include <sstream>
#include <string>
#include <vector>
#include <functional>
#include <deque>
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <rlgl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
extern "C"
{
    int ed_native_folder(const char *, char *, int);
    int ed_native_file(int, int, const char *, char *, int);
    int ed_native_guard();
    int ed_native_replace(const char *, const char *);
    void ed_native_init();
    void ed_native_close();
    void ed_native_menu(void *window);
    int ed_native_command();
    void ed_native_menu_state(int play, int selected, int undo, int redo);
}

#ifdef _WIN32
#define EDITOR_API extern "C" __declspec(dllexport)
#else
#define EDITOR_API extern "C" __attribute__((visibility("default")))
#endif
EDITOR_API void ed_workspace_init();
EDITOR_API void ed_workspace_close();
EDITOR_API Vector2 ed_pointer();
EDITOR_API int ed_input_blocked();
EDITOR_API void ed_capture(int);
extern "C"
{
    int ox_texture(const char *);
    int ox_model(const char *);
    void ox_release_texture(int);
    void ox_release_model(int);
    Texture2D ox_lookup_texture(int);
    int ox_model_bounds(int, float *);
    void ox_draw_model(int, float, float, float, float, float, float, float, float, float, unsigned);
    void ox_lights_reset(float, float, float);
    int ox_light(int, float, float, float, unsigned, float, float);
}

extern "C" void ed_gizmo_context(float, float, float, float, float, float, float, float, float, float, float);
extern "C" const char *ed_gizmo_hint();
extern "C" void ed_gizmo_init();
extern "C" void ed_gizmo_close();
extern "C" void ed_gizmo_frame();

namespace fs = std::filesystem;
namespace
{
constexpr int stride = 15, capacity = 4096;
struct Asset
{
    int id, kind, handle = 0;
    fs::path relative;
    bool failed = false;
};
std::vector<Asset> assets;
fs::path root, currentScene, folder;
std::string status = "Paint your first room. Right mouse + WASD to navigate.";
int selectedAsset = 0, assetScroll = 0, hierarchyScroll = 0, selectedRow = -1;
bool audioReady = false;
bool audioAttempted = false;
Sound sound{};
struct Voice
{
    int slot, asset;
    Sound sound;
};
std::vector<Voice> voices;
RenderTexture2D viewport{}, preview{};
Rectangle view{};
Camera3D viewCamera{};
bool pressedKeys[512]{};
const char *names[] = {"Empty",  "Floor", "Block",       "Wall",  "Ramp",
                       "Column", "Model", "Point light", "Sound", "Player start"};
bool smoke()
{
    return std::getenv("OXYGEN_EDITOR_SMOKE") != nullptr;
}
void message(const std::string &value)
{
    status = value;
}
std::string utf8(const fs::path &p)
{
    return p.u8string();
}
fs::path fromUtf8(const char *p)
{
    return fs::u8path(p);
}
bool safeRelative(const fs::path &p)
{
    if (p.empty() || p.is_absolute() || p.has_root_name())
    {
        return false;
    }
    for (const auto &part : p)
    {
        if (part == "..")
        {
            return false;
        }
    }
    return true;
}
int kind(const fs::path &p)
{
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
    {
        return 1;
    }
    if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".iqm")
    {
        return 2;
    }
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac")
    {
        return 3;
    }
    return 4;
}
Asset *asset(int id)
{
    for (auto &a : assets)
    {
        if (a.id == id)
        {
            return &a;
        }
    }
    return nullptr;
}
int registerAsset(const fs::path &relative)
{
    if (!safeRelative(relative) || assets.size() >= 512)
    {
        return 0;
    }
    for (auto &a : assets)
    {
        if (a.relative == relative)
        {
            return a.id;
        }
    }
    // IDs are exactly representable in the Xenon float record. Files store paths, never IDs.
    int id = (int)assets.size() + 1;
    assets.push_back({id, kind(relative), 0, relative, false});
    return id;
}
void scan()
{
    try
    {
        std::vector<fs::path> found;
        for (auto &entry :
             fs::recursive_directory_iterator(root / "Assets", fs::directory_options::skip_permission_denied))
        {
            if (entry.is_regular_file() && !entry.is_symlink())
            {
                found.push_back(entry.path().lexically_relative(root / "Assets"));
            }
        }
        std::sort(found.begin(), found.end());
        for (auto &path : found)
        {
            registerAsset(path);
        }
    }
    catch (const std::exception &e)
    {
        message(std::string("Asset scan failed: ") + e.what());
    }
}
void stopSound()
{
    if (sound.frameCount)
    {
        StopSound(sound);
        UnloadSound(sound);
        sound = {};
    }
}
void stopVoices()
{
    for (auto &voice : voices)
    {
        StopSound(voice.sound);
        UnloadSound(voice.sound);
    }
    voices.clear();
}
void clearAssets()
{
    stopVoices();
    stopSound();
    for (auto &a : assets)
    {
        if (a.kind == 1 && a.handle)
        {
            ox_release_texture(a.handle);
        }
        if (a.kind == 2 && a.handle)
        {
            ox_release_model(a.handle);
        }
    }
    assets.clear();
    selectedAsset = 0;
    folder.clear();
    assetScroll = 0;
}
int handle(Asset *a)
{
    if (!a || a->failed)
    {
        return 0;
    }
    if (!a->handle && (a->kind == 1 || a->kind == 2))
    {
        std::string path = utf8(root / "Assets" / a->relative);
        if (!fs::exists(root / "Assets" / a->relative))
        {
            a->failed = true;
            message("Missing asset: " + utf8(a->relative));
            return 0;
        }
        a->handle = a->kind == 1 ? ox_texture(path.c_str()) : ox_model(path.c_str());
        if (!a->handle)
        {
            a->failed = true;
            message("Cannot load asset: " + utf8(a->relative));
        }
    }
    return a->handle;
}
fs::path chooseFolder(const char *title)
{
    char path[32768]{};
    return ed_native_folder(title, path, sizeof(path)) ? fs::u8path(path) : fs::path();
}
fs::path chooseFile(bool save, bool scene)
{
    char path[32768]{};
    std::string initial = utf8(root / (scene ? "Scenes" : "Assets"));
    return ed_native_file(save, scene, initial.c_str(), path, sizeof(path)) ? fs::u8path(path) : fs::path();
}
bool openProject(const fs::path &path, bool create)
{
    if (path.empty())
    {
        return false;
    }
    try
    {
        if (!create && !fs::exists(path / "oxygen.project"))
        {
            message("Choose a folder containing oxygen.project.");
            return false;
        }
        if (fs::exists(path / "oxygen.project"))
        {
            std::ifstream marker(path / "oxygen.project");
            std::string magic;
            int version = 0;
            marker >> magic >> version;
            if (!marker || magic != "OXYGEN_PROJECT" || version != 1)
            {
                message("Unsupported Oxygen project format.");
                return false;
            }
        }
        fs::create_directories(path / "Assets");
        fs::create_directories(path / "Scenes");
        if (!fs::exists(path / "oxygen.project"))
        {
            std::ofstream f(path / "oxygen.project");
            f << "OXYGEN_PROJECT 1\n";
            if (!f)
            {
                return false;
            }
        }
        clearAssets();
        root = fs::absolute(path).lexically_normal();
        currentScene = root / "Scenes" / "Main.oxmap";
        scan();
        message("Project: " + utf8(root.filename()));
        return true;
    }
    catch (const std::exception &e)
    {
        message(e.what());
        return false;
    }
}
void seedResources()
{
    // Original tiny fixtures, generated locally; no third-party asset downloads.
    fs::path materials = root / "Assets" / "Materials", props = root / "Assets" / "Props",
             sounds = root / "Assets" / "Sounds";
    fs::create_directories(materials);
    fs::create_directories(props);
    fs::create_directories(sounds);
    if (!fs::exists(materials / "Stone.png"))
    {
        Image image = GenImageChecked(128, 128, 16, 16, {154, 171, 184, 255}, {113, 132, 147, 255});
        ExportImage(image, utf8(materials / "Stone.png").c_str());
        UnloadImage(image);
    }
    if (!fs::exists(props / "Crate.obj"))
    {
        std::ofstream model(props / "Crate.obj");
        model << "# Oxygen original sample prop\nmtllib Crate.mtl\no Crate\n"
                 "v -0.5 0 -0.5\nv 0.5 0 -0.5\nv 0.5 1 -0.5\nv -0.5 1 -0.5\n"
                 "v -0.5 0 0.5\nv 0.5 0 0.5\nv 0.5 1 0.5\nv -0.5 1 0.5\n"
                 "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                 "vn 0 0 -1\nvn 0 0 1\nvn -1 0 0\nvn 1 0 0\nvn 0 1 0\nvn 0 -1 0\n"
                 "usemtl Wood\nf 4/4/1 3/3/1 2/2/1 1/1/1\nf 5/1/2 6/2/2 7/3/2 8/4/2\n"
                 "f 1/1/3 5/2/3 8/3/3 4/4/3\nf 2/1/4 3/4/4 7/3/4 6/2/4\n"
                 "f 4/1/5 8/2/5 7/3/5 3/4/5\nf 1/1/6 2/2/6 6/3/6 5/4/6\n";
        std::ofstream material(props / "Crate.mtl");
        material << "newmtl Wood\nKd 0.72 0.46 0.24\n";
    }
    if (!fs::exists(sounds / "Chime.wav"))
    {
        std::vector<short> samples(22050);
        for (size_t i = 0; i < samples.size(); i++)
        {
            float t = (float)i / 22050.f;
            samples[i] = (short)(std::sin(t * 440.f * 2 * PI) * std::exp(-t * 6) * 7000);
        }
        Wave wave{(unsigned)samples.size(), 22050, 16, 1, samples.data()};
        ExportWave(wave, utf8(sounds / "Chime.wav").c_str());
    }
    scan();
}
bool copyResource(const fs::path &source, const fs::path &relative)
{
    if (!safeRelative(relative) || fs::is_symlink(source))
    {
        return false;
    }
    fs::path dest = root / "Assets" / relative;
    if (fs::exists(dest))
    {
        return false; // Never overwrite an existing asset implicitly.
    }
    fs::create_directories(dest.parent_path());
    return fs::copy_file(source, dest, fs::copy_options::none);
}
void importPath(const fs::path &path)
{
    try
    {
        int copied = 0;
        if (fs::is_directory(path))
        {
            // Do not recursively import a project into its own Assets folder.
            auto rel = root.lexically_relative(fs::absolute(path));
            if (!rel.empty() && *rel.begin() != "..")
            {
                message("Choose a resource folder outside this project.");
                return;
            }
            for (auto &f :
                 fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied))
            {
                if (f.is_regular_file() && !f.is_symlink())
                {
                    copied += copyResource(f.path(), path.filename() / f.path().lexically_relative(path));
                }
            }
        }
        else if (fs::is_regular_file(path))
        {
            copied += copyResource(path, path.filename());
            // OBJ/glTF sidecars are preserved when importing their containing folder.
        }
        scan();
        message("Imported " + std::to_string(copied) + " file(s). Existing files were kept.");
    }
    catch (const std::exception &e)
    {
        message(std::string("Import failed: ") + e.what());
    }
}
bool valid(const float *r)
{
    for (int j = 0; j < stride; j++)
    {
        if (!std::isfinite(r[j]))
        {
            return false;
        }
    }
    int k = (int)r[0];
    return r[0] == k && k >= 1 && k <= 9 && std::fabs(r[1]) <= 10000 && std::fabs(r[2]) <= 10000 &&
           std::fabs(r[3]) <= 10000 && std::fabs(r[4]) <= 36000 && r[5] >= .02f && r[5] <= 1000 &&
           r[6] >= .02f && r[6] <= 1000 && r[7] >= .02f && r[7] <= 1000 && r[8] >= 0 && r[8] <= 512 &&
           r[8] == (int)r[8] && r[9] >= 0 && r[9] <= 5 && r[10] >= 0 && r[10] <= 2 && r[11] >= 0 &&
           r[11] <= 32 && std::fabs(r[12]) <= 36000 && std::fabs(r[13]) <= 36000 && r[14] >= 0 &&
           r[14] <= capacity && r[14] == (int)r[14];
}
bool validHierarchy(const float *data)
{
    for (int i = 0; i < capacity; i++)
    {
        if (!data[i * stride])
        {
            continue;
        }
        int current = i;
        for (int depth = 0;; depth++)
        {
            if (depth >= 64)
            {
                return false;
            }
            current = (int)data[current * stride + 14] - 1;
            if (current < 0)
            {
                break;
            }
            if (current >= capacity || current == i || !data[current * stride])
            {
                return false;
            }
        }
    }
    return true;
}
bool saveRecords(const fs::path &path, const float *data, int count)
{
    try
    {
        if (count != capacity)
        {
            return false;
        }
        int used = 0;
        for (int i = 0; i < count; i++)
        {
            if (data[i * stride])
            {
                if (!valid(data + i * stride))
                {
                    message("Invalid scene data; save cancelled.");
                    return false;
                }
                used++;
            }
        }
        if (!validHierarchy(data))
        {
            message("Invalid hierarchy; save cancelled.");
            return false;
        }
        fs::create_directories(path.parent_path());
        fs::path temp = path;
        temp += ".tmp";
        std::ofstream f(temp, std::ios::trunc);
        f << "OXYGEN_TILE_SCENE 3\n" << used << "\n" << std::setprecision(9);
        for (int i = 0; i < count; i++)
        {
            const float *r = data + i * stride;
            if (!r[0])
            {
                continue;
            }
            f << i << ' ';
            for (int j = 0; j < stride; j++)
            {
                f << (j == 8 ? 0 : r[j]) << ' ';
            }
            Asset *a = asset((int)r[8]);
            f << std::quoted(a ? utf8(a->relative) : std::string()) << '\n';
        }
        f.flush();
        bool ok = (bool)f;
        f.close();
        if (!ok || !ed_native_replace(utf8(temp).c_str(), utf8(path).c_str()))
        {
            message("Save failed; previous scene retained.");
            return false;
        }
        currentScene = path;
        message("Saved " + utf8(path.filename()));
        return true;
    }
    catch (const std::exception &e)
    {
        message(e.what());
        return false;
    }
}
bool loadRecords(const fs::path &path, float *data, int count)
{
    try
    {
        std::ifstream f(path);
        std::string magic;
        int version = 0, n = 0;
        f >> magic >> version >> n;
        if (!f || magic != "OXYGEN_TILE_SCENE" || (version < 1 || version > 3) || n < 0 || n > count ||
            count != capacity)
        {
            return false;
        }
        std::vector<float> records(count * stride, 0);
        std::vector<fs::path> refs(count);
        for (int i = 0; i < n; i++)
        {
            int slot = i;
            if (version == 3)
            {
                f >> slot;
            }
            if (!f || slot < 0 || slot >= count || records[slot * stride] != 0)
            {
                return false;
            }
            float *r = records.data() + slot * stride;
            for (int j = 0; j < (version == 1 ? 12 : version == 2 ? 14 : stride); j++)
            {
                f >> r[j];
            }
            std::string reference;
            f >> std::quoted(reference);
            refs[slot] = fs::u8path(reference);
            if (!f || !valid(r) || (!reference.empty() && !safeRelative(refs[slot])))
            {
                return false;
            }
        }
        f >> std::ws;
        if (!f.eof())
        {
            return false;
        }
        if (!validHierarchy(records.data()))
        {
            return false;
        }
        for (int i = 0; i < count; i++)
        {
            if (!refs[i].empty())
            {
                int id = registerAsset(refs[i]);
                if (!id)
                {
                    return false;
                }
                records[i * stride + 8] = (float)id;
            }
        }
        std::copy(records.begin(), records.end(), data);
        currentScene = path;
        message("Loaded " + utf8(path.filename()));
        return true;
    }
    catch (...)
    {
        return false;
    }
}
void renderModelPreview(int id)
{
    Asset *a = asset(id);
    if (!a || a->kind != 2)
    {
        return;
    }
    int model = handle(a);
    if (!model)
    {
        return;
    }
    if (!preview.id)
    {
        preview = LoadRenderTexture(228, 100);
    }
    float bounds[6]{};
    ox_model_bounds(model, bounds);
    Vector3 center{(bounds[0] + bounds[3]) * .5f, (bounds[1] + bounds[4]) * .5f,
                   (bounds[2] + bounds[5]) * .5f};
    float radius =
        std::max(.1f, Vector3Distance({bounds[0], bounds[1], bounds[2]}, {bounds[3], bounds[4], bounds[5]}));
    Camera3D camera{Vector3Add(center, {radius * .8f, radius * .5f, radius * .8f}),
                    center,
                    {0, 1, 0},
                    45,
                    CAMERA_PERSPECTIVE};
    BeginTextureMode(preview);
    ClearBackground({22, 27, 35, 255});
    BeginMode3D(camera);
    ox_lights_reset(.5f, .5f, .5f);
    ox_light(0, -1, -1, -1, 0xFFFFFFFFu, .8f, 1);
    ox_draw_model(model, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0xFFFFFFFFu);
    EndMode3D();
    EndTextureMode();
}
} // namespace

EDITOR_API int ed_init()
{
    ed_native_init();
    ed_native_menu(GetWindowHandle());
    ed_workspace_init();
    ed_gizmo_init();
    SetWindowMinSize(1100, 850);
    const char *project = std::getenv("OXYGEN_EDITOR_PROJECT");
    fs::path initial =
        project ? fromUtf8(project) : fs::path(GetApplicationDirectory()) / "../../../Projects/Starter";
    bool fresh = !fs::exists(initial / "oxygen.project");
    if (!openProject(initial, true))
    {
        return 0;
    }
    if (fresh)
    {
        try
        {
            seedResources();
        }
        catch (...)
        {
            message("Sample resources could not be generated.");
        }
    }
    return 1;
}
EDITOR_API void ed_close()
{
    ed_gizmo_close();
    ed_capture(0);
    ed_workspace_close();
    clearAssets();
    if (audioReady)
    {
        CloseAudioDevice();
    }
    if (viewport.id)
    {
        UnloadRenderTexture(viewport);
    }
    if (preview.id)
    {
        UnloadRenderTexture(preview);
    }
    ed_native_close();
}
EDITOR_API int ed_guard(int dirty)
{
    if (!dirty || smoke())
    {
        return 1;
    }
    return ed_native_guard();
}
EDITOR_API int ed_project(int create)
{
    return openProject(chooseFolder(create ? "New Oxygen project folder" : "Open Oxygen project"),
                       create != 0);
}
EDITOR_API int ed_save(const float *data, int count)
{
    fs::path path = currentScene;
    if (path.empty())
    {
        path = smoke() ? root / "Scenes" / "Main.oxmap" : chooseFile(true, true);
    }
    return !path.empty() && saveRecords(path, data, count);
}
EDITOR_API int ed_save_as(const float *data, int count)
{
    fs::path p = chooseFile(true, true);
    return !p.empty() && saveRecords(p, data, count);
}
EDITOR_API int ed_load(float *data, int count, int automatic)
{
    fs::path p = automatic ? currentScene : chooseFile(false, true);
    if (p.empty())
    {
        return 0;
    }
    if (loadRecords(p, data, count))
    {
        return 1;
    }
    if (fs::exists(p))
    {
        message("Scene rejected: invalid or unsupported data. Current scene kept.");
    }
    return 0;
}
EDITOR_API void ed_new_scene()
{
    currentScene.clear();
}
EDITOR_API int ed_asset_id()
{
    return selectedAsset;
}
EDITOR_API int ed_asset_find(const char *path)
{
    for (auto &a : assets)
    {
        if (a.relative == fs::u8path(path))
        {
            return a.id;
        }
    }
    return 0;
}
EDITOR_API int ed_asset_kind(int id)
{
    auto *a = asset(id);
    return a ? a->kind : 0;
}
EDITOR_API int ed_asset_handle(int id)
{
    return handle(asset(id));
}
EDITOR_API void ed_status(const char *text)
{
    message(text);
}
EDITOR_API int ed_hierarchy_selection()
{
    return selectedRow;
}
EDITOR_API void ed_import(int directory)
{
    auto p = directory ? chooseFolder("Import resource folder (keeps OBJ / glTF dependencies)")
                       : chooseFile(false, false);
    if (!p.empty())
    {
        importPath(p);
    }
}
EDITOR_API void ed_sound(int id, float volume)
{
    stopSound();
    auto *a = asset(id);
    if (!a || a->kind != 3)
    {
        return;
    }
    if (!audioReady)
    {
        InitAudioDevice();
        audioReady = IsAudioDeviceReady();
    }
    if (!audioReady)
    {
        message("Audio device unavailable.");
        return;
    }
    sound = LoadSound(utf8(root / "Assets" / a->relative).c_str());
    if (sound.frameCount)
    {
        SetSoundVolume(sound, std::clamp(volume, 0.f, 1.f));
        PlaySound(sound);
    }
    else
    {
        message("Audio could not be loaded.");
    }
}
EDITOR_API void ed_stop_sound()
{
    stopSound();
    stopVoices();
}
EDITOR_API void ed_voice(int slot, int id, float volume)
{
    if (!audioReady && !audioAttempted)
    {
        audioAttempted = true;
        InitAudioDevice();
        audioReady = IsAudioDeviceReady();
    }
    if (!audioReady)
    {
        return;
    }
    Voice *found = nullptr;
    for (auto &voice : voices)
    {
        if (voice.slot == slot)
        {
            found = &voice;
            break;
        }
    }
    if (!found)
    {
        auto *a = asset(id);
        if (!a || a->kind != 3 || voices.size() >= 16)
        {
            return;
        }
        Sound loaded = LoadSound(utf8(root / "Assets" / a->relative).c_str());
        voices.push_back({slot, id, loaded});
        found = &voices.back();
    }
    if (!found->sound.frameCount)
    {
        return;
    }
    if (smoke())
    {
        volume = 0;
    }
    SetSoundVolume(found->sound, std::clamp(volume, 0.f, 1.f));
    if (!IsSoundPlaying(found->sound))
    {
        PlaySound(found->sound);
    }
}
EDITOR_API int ed_storage_test(const float *data)
{
    if (!smoke())
    {
        return 0;
    }
    std::vector<float> original(data, data + capacity * stride), copy(original);
    fs::path test = root / "Scenes" / "invalid-regression.oxmap";
    {
        std::ofstream f(test);
        f << "OXYGEN_TILE_SCENE 1\n1\n1 0 0 0 0 2 2 2 0 0 0 1 \"../escape.png\"\n";
    }
    if (loadRecords(test, copy.data(), capacity) || copy != original)
    {
        return 1;
    }
    fs::path previousScene = currentScene;
    fs::path legacy = root / "Scenes" / "legacy-gizmo-test.oxmap";
    {
        std::ofstream f(legacy);
        f << "OXYGEN_TILE_SCENE 1\n1\n2 0 0 0 45 2 2 2 0 0 0 1 \"\"\n";
    }
    bool compatible =
        loadRecords(legacy, copy.data(), capacity) && copy[4] == 45 && copy[12] == 0 && copy[13] == 0;
    copy[12] = 30;
    copy[13] = -25;
    fs::path rotated = root / "Scenes" / "rotation-gizmo-test.oxmap";
    bool saved = saveRecords(rotated, copy.data(), capacity);
    copy.assign(copy.size(), 0);
    bool restored = saved && loadRecords(rotated, copy.data(), capacity) && copy[4] == 45 && copy[12] == 30 &&
                    copy[13] == -25;
    // Sparse IDs survive saves so parent references cannot silently target another object.
    std::copy(copy.begin(), copy.begin() + stride, copy.begin() + 48 * stride);
    copy[48 * stride + 14] = 1;
    bool hierarchySaved = saveRecords(rotated, copy.data(), capacity);
    copy.assign(copy.size(), 0);
    bool hierarchyLoaded = hierarchySaved && loadRecords(rotated, copy.data(), capacity) &&
                           copy[48 * stride] == 2 && copy[48 * stride + 14] == 1 && copy[stride] == 0;
    {
        std::ofstream f(test);
        f << "OXYGEN_TILE_SCENE 3\n2\n";
        f << "0 2 0 0 0 0 2 2 2 0 0 0 1 0 0 49 \"\"\n";
        f << "48 2 0 0 0 0 2 2 2 0 0 0 1 0 0 1 \"\"\n";
    }
    auto retained = copy;
    bool cycleRejected = !loadRecords(test, copy.data(), capacity) && copy == retained;
    currentScene = previousScene;
    if (!compatible || !restored || !hierarchyLoaded || !cycleRejected)
    {
        return 1;
    }
    copy = original;
    {
        std::ofstream f(test);
        f << "OXYGEN_TILE_SCENE 1\n4097\n";
    }
    if (loadRecords(test, copy.data(), capacity) || copy != original)
    {
        return 1;
    }
    {
        std::ofstream f(test);
        f << "OXYGEN_TILE_SCENE 1\n1\n2 0 0 0 0 -2 2 2 0 0 0 1 \"\"\n";
    }
    if (loadRecords(test, copy.data(), capacity) || copy != original)
    {
        return 1;
    }
    puts(
        "PASS: malformed scenes, capacity and escaping asset paths are rejected without modifying the document.");
    puts("PASS: legacy scene migration, XYZ rotation, sparse hierarchy persistence and cycle rejection.");
    return 0;
}
EDITOR_API void ed_ray(float *out, float x, float y, float z, float fx, float fy, float fz, float fov)
{
    ed_gizmo_context(x, y, z, fx, fy, fz, fov, view.x, view.y, view.width, view.height);
    Camera3D c{{x, y, z}, {x + fx, y + fy, z + fz}, {0, 1, 0}, fov, CAMERA_PERSPECTIVE};
    Vector2 mouse = ed_pointer();
    mouse.x -= view.x;
    mouse.y -= view.y;
    Ray ray = GetScreenToWorldRayEx(mouse, c, (int)view.width, (int)view.height);
    out[0] = ray.position.x;
    out[1] = ray.position.y;
    out[2] = ray.position.z;
    out[3] = ray.direction.x;
    out[4] = ray.direction.y;
    out[5] = ray.direction.z;
}
EDITOR_API int ed_pick(const float *ray, float x, float y, float z, float sx, float sy, float sz, float yaw,
                       float pitch, float roll, float *distance)
{
    Quaternion inverse =
        QuaternionInvert(QuaternionFromEuler(pitch * DEG2RAD, yaw * DEG2RAD, roll * DEG2RAD));
    Vector3 from =
        Vector3RotateByQuaternion(Vector3Subtract({ray[0], ray[1], ray[2]}, {x, y + sy * .5f, z}), inverse);
    Vector3 dir = Vector3RotateByQuaternion({ray[3], ray[4], ray[5]}, inverse);
    RayCollision hit =
        GetRayCollisionBox({from, dir}, {{-sx * .5f, -sy * .5f, -sz * .5f}, {sx * .5f, sy * .5f, sz * .5f}});
    if (hit.hit)
    {
        *distance = hit.distance;
    }
    return hit.hit;
}
EDITOR_API void ed_view_begin()
{
    int w = (int)view.width, h = (int)view.height;
    if (viewport.texture.width != w || viewport.texture.height != h)
    {
        if (viewport.id)
        {
            UnloadRenderTexture(viewport);
        }
        viewport = LoadRenderTexture(w, h);
        SetTextureFilter(viewport.texture, TEXTURE_FILTER_BILINEAR);
    }
    BeginTextureMode(viewport);
    ClearBackground({30, 38, 49, 255});
}
EDITOR_API void ed_view_end(int playing)
{
    EndTextureMode();
}
EDITOR_API void ed_grid(float height)
{
    for (int i = -16; i <= 16; i++)
    {
        Color c = i == 0 ? Color{85, 130, 136, 255} : Color{65, 78, 92, 255};
        DrawLine3D({(float)i * 2, height, -32}, {(float)i * 2, height, 32}, c);
        DrawLine3D({-32, height, (float)i * 2}, {32, height, (float)i * 2}, c);
    }
}
EDITOR_API void ed_outline(float x, float y, float z, float sx, float sy, float sz, unsigned color)
{
    DrawCubeWires({x, y + sy * .5f, z}, sx + .02f, sy + .02f, sz + .02f, GetColor(color));
}
#include "editor_workspace.inl"

EDITOR_API int ed_validate_scene(const float *records)
{
    for (int i = 0; i < capacity; i++)
    {
        if (records[i * stride] != 0 && !valid(records + i * stride))
        {
            return 0;
        }
    }
    return validHierarchy(records);
}

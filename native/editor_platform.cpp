// Editor platform services. Scene commands, history, tile tools and play mode live in Xenon.
#include <algorithm>
#include <array>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <imgui.h>
#include <imgui_internal.h>
#include <iomanip>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
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
EDITOR_API int ed_tile_guard();
EDITOR_API void ed_tile_reset();
EDITOR_API void ed_tile_close();
int tileLoadModel(const std::filesystem::path &relative);
namespace
{
void tilePalette(const float *state);
extern bool tileFocused;
extern bool tileCatalogDirty;
} // namespace
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
constexpr int stride = 16, capacity = 4096;
constexpr float sceneGridSpacing = 2.f;
constexpr int maxWorldLayers = 4096;
std::vector<std::string> worldLayers{"Layer 1"};
// Editor-only flags: hidden = 1, locked = 2. Missing entries are visible and editable.
std::unordered_map<std::string, int> layerFlags;
int worldLayerFlags(int index)
{
    if (index < 0 || index >= (int)worldLayers.size())
    {
        return 3;
    }
    auto found = layerFlags.find(worldLayers[index]);
    return found == layerFlags.end() ? 0 : found->second;
}
using TileCodecCallback = int (*)(int, const char *, float *, char *, char *);
using TileMeshCallback = int (*)(float *, float *, unsigned short *, int *);
TileCodecCallback tileCodecCallback = nullptr;
TileMeshCallback tileMeshCallback = nullptr;
using TileSelectionCallback = int (*)(int, float *, float *, float *, float *);
TileSelectionCallback tileSelectionCallback = nullptr;
using TileSetsCallback = int (*)(int, const char *, int, const char **, const char *, int);
TileSetsCallback tileSetsCallback = nullptr;
using ProjectCallback = int (*)(int, const char *, int);
ProjectCallback projectCallback = nullptr;
using WorkflowCallback = int (*)(int, const char *, float *, int);
WorkflowCallback workflowCallback = nullptr;
using LayersCallback = int (*)(int, int, int, const char *);
LayersCallback layersCallback = nullptr;
void replaceLayers(std::vector<std::string> names, std::unordered_map<std::string, int> flags = {})
{
    layersCallback(8, 0, 0, nullptr);
    for (size_t i = 0; i < names.size(); i++)
    {
        layersCallback(1, 0, 0, names[i].c_str());
        layersCallback(3, (int)i, flags[names[i]], nullptr);
    }
}
int addWorldLayer(const std::string &input)
{
    return layersCallback(1, 0, 0, input.c_str());
}
const char *worldLayerName(int index)
{
    return index >= 0 && index < (int)worldLayers.size() ? worldLayers[index].c_str() : "Invalid layer";
}
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
int kind(const fs::path &p)
{
    return tileCodecCallback(3, utf8(p).c_str(), nullptr, nullptr, nullptr);
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
    return projectCallback(1, utf8(relative).c_str(), 0);
}
void scan()
{
    projectCallback(2, nullptr, 0);
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
void clearNativeAssetViews()
{
    stopVoices();
    stopSound();
    for (auto &a : assets)
    {
        if (a.kind == 1 && a.handle)
        {
            ox_release_texture(a.handle);
        }
        if ((a.kind == 2 || a.kind == 5) && a.handle)
        {
            ox_release_model(a.handle);
        }
    }
    assets.clear();
    selectedAsset = 0;
    folder.clear();
    assetScroll = 0;
}
void clearAssets()
{
    projectCallback(4, nullptr, 0);
}
int handle(Asset *a)
{
    if (!a || a->failed)
    {
        return 0;
    }
    if (!a->handle && (a->kind == 1 || a->kind == 2 || a->kind == 5))
    {
        std::string path = utf8(root / "Assets" / a->relative);
        if (!fs::exists(root / "Assets" / a->relative))
        {
            a->failed = true;
            message("Missing asset: " + utf8(a->relative));
            return 0;
        }
        a->handle = a->kind == 1   ? ox_texture(path.c_str())
                    : a->kind == 5 ? tileLoadModel(a->relative)
                                   : ox_model(path.c_str());
        if (!a->handle)
        {
            a->failed = true;
            message("Cannot load asset: " + utf8(a->relative));
        }
    }
    return a->handle;
}
bool openProject(const fs::path &path, bool create)
{
    return projectCallback(0, utf8(path).c_str(), create ? 1 : 0) != 0;
}
void importPath(const fs::path &path)
{
    projectCallback(3, utf8(path).c_str(), 0);
}
using SceneCallback = int (*)(int, const char *, float *, int);
SceneCallback sceneCallback = nullptr;
bool valid(const float *r)
{
    return sceneCallback(2, nullptr, const_cast<float *>(r), stride) != 0;
}
bool validHierarchy(const float *data)
{
    return sceneCallback(3, nullptr, const_cast<float *>(data), capacity) != 0;
}
bool saveRecords(const fs::path &path, const float *data, int count)
{
    return workflowCallback(8, utf8(path).c_str(), const_cast<float *>(data), count) != 0;
}
bool loadRecords(const fs::path &path, float *data, int count)
{
    return workflowCallback(9, utf8(path).c_str(), data, count) != 0;
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
    return workflowCallback(0, nullptr, nullptr, 0);
}
EDITOR_API void ed_close()
{
    ed_tile_close();
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
    return workflowCallback(10, nullptr, nullptr, dirty);
}
EDITOR_API int ed_project(int create)
{
    return workflowCallback(6, nullptr, nullptr, create);
}
EDITOR_API int ed_save(const float *data, int count)
{
    return workflowCallback(2, nullptr, const_cast<float *>(data), count);
}
EDITOR_API int ed_save_as(const float *data, int count)
{
    return workflowCallback(3, nullptr, const_cast<float *>(data), count);
}
EDITOR_API int ed_load(float *data, int count, int automatic)
{
    return workflowCallback(4, automatic ? "" : nullptr, data, count);
}
EDITOR_API void ed_bind_tile_codec(TileCodecCallback callback)
{
    tileCodecCallback = callback;
}
EDITOR_API void ed_bind_tile_mesh(TileMeshCallback callback)
{
    tileMeshCallback = callback;
}
EDITOR_API void ed_bind_tile_selection(TileSelectionCallback callback)
{
    tileSelectionCallback = callback;
}
EDITOR_API void ed_project_point(float *values, float x, float y, float z, int width, int height,
                                 float *screen)
{
    Camera3D camera{{values[0], values[1], values[2]},
                    {values[3], values[4], values[5]},
                    {values[6], values[7], values[8]},
                    values[9],
                    CAMERA_PERSPECTIVE};
    Vector2 projected = GetWorldToScreenEx({x, y, z}, camera, width, height);
    screen[0] = projected.x;
    screen[1] = projected.y;
}
EDITOR_API void ed_bind_tilesets(TileSetsCallback callback)
{
    tileSetsCallback = callback;
}
#include "editor_files.inl"
EDITOR_API void ed_bind_project(ProjectCallback callback)
{
    projectCallback = callback;
}
EDITOR_API void ed_assets_view_clear()
{
    clearNativeAssetViews();
}
EDITOR_API void ed_assets_view_item(int id, int kind, const char *relative)
{
    assets.push_back({id, kind, 0, fromUtf8(relative), false});
}
EDITOR_API void ed_project_view_root(const char *path)
{
    root = fromUtf8(path);
    currentScene = root / "Scenes" / "Main.oxmap";
}
EDITOR_API void ed_screen_ray(float *values, float x, float y, int width, int height, float *result)
{
    Camera3D camera{{values[0], values[1], values[2]},
                    {values[3], values[4], values[5]},
                    {values[6], values[7], values[8]},
                    values[9],
                    CAMERA_PERSPECTIVE};
    Ray ray = GetScreenToWorldRayEx({x, y}, camera, width, height);
    result[0] = ray.position.x;
    result[1] = ray.position.y;
    result[2] = ray.position.z;
    result[3] = ray.direction.x;
    result[4] = ray.direction.y;
    result[5] = ray.direction.z;
}
EDITOR_API void ed_bind_scene(SceneCallback callback)
{
    sceneCallback = callback;
}
EDITOR_API int ed_atomic_replace(const char *from, const char *to)
{
    return ed_native_replace(from, to);
}
EDITOR_API const char *ed_asset_path(int id)
{
    static std::string result;
    auto *item = asset(id);
    result = item ? utf8(item->relative) : std::string();
    return result.c_str();
}
EDITOR_API int ed_register_asset(const char *relative)
{
    return registerAsset(fromUtf8(relative));
}
EDITOR_API void ed_bind_layers(LayersCallback callback)
{
    layersCallback = callback;
}
EDITOR_API void ed_layers_view_begin()
{
    worldLayers.clear();
    layerFlags.clear();
}
EDITOR_API void ed_layers_view_item(const char *name, int flags)
{
    worldLayers.emplace_back(name);
    layerFlags[name] = flags;
}
EDITOR_API void ed_new_scene()
{
    workflowCallback(5, nullptr, nullptr, 0);
}
EDITOR_API int ed_layer_count()
{
    return (int)worldLayers.size();
}
EDITOR_API int ed_layer_add(const char *name)
{
    return layersCallback(1, 0, 0, name);
}
EDITOR_API int ed_layer_visible(int index)
{
    return (worldLayerFlags(index) & 1) == 0;
}
EDITOR_API int ed_layer_editable(int index)
{
    return worldLayerFlags(index) == 0;
}
EDITOR_API int ed_layer_set_flags(int index, int flags)
{
    return layersCallback(3, index, flags, nullptr);
}
EDITOR_API int ed_layer_rename(int index, const char *input)
{
    return layersCallback(2, index, 0, input);
}
EDITOR_API int ed_layer_remove(int index)
{
    return layersCallback(4, index, 0, nullptr);
}
EDITOR_API int ed_layers_snapshot(int active)
{
    return layersCallback(5, active, 0, nullptr);
}
EDITOR_API int ed_layers_restore(int id)
{
    return layersCallback(6, id, 0, nullptr);
}
EDITOR_API void ed_layers_release(int id)
{
    layersCallback(7, id, 0, nullptr);
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
    workflowCallback(7, nullptr, nullptr, directory);
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
EDITOR_API int ed_layer_tests()
{
    if (!smoke())
    {
        return 0;
    }
    auto previousLayers = worldLayers;
    auto previousFlags = layerFlags;
    auto previousScene = currentScene;
    ed_new_scene();
    int failures = worldLayers != std::vector<std::string>{"Layer 1"};
    if (addWorldLayer("  Decorations  ") != 1 || worldLayers.back() != "Decorations" ||
        addWorldLayer("Decorations") != -1 || addWorldLayer("   ") != -1)
    {
        failures++;
    }
    for (int i = 2; i < 12; i++)
    {
        if (addWorldLayer("Custom " + std::to_string(i)) != i)
        {
            failures++;
        }
    }
    if (addWorldLayer("Декорации \"верх\"") != 12)
    {
        failures++;
    }
    auto expectedLayers = worldLayers;
    std::vector<float> records(capacity * stride, 0), restored(records);
    records[0] = 2;
    records[1] = records[3] = 1;
    records[5] = records[6] = records[7] = 2;
    records[11] = 2;
    records[15] = 12;
    auto path = root / "Scenes" / "custom-layers-regression.oxmap";
    if (!saveRecords(path, records.data(), capacity))
    {
        failures++;
    }
    ed_new_scene();
    if (!loadRecords(path, restored.data(), capacity) || restored != records || worldLayers != expectedLayers)
    {
        failures++;
    }
    auto retained = restored;
    {
        std::ofstream f(path);
        f << "OXYGEN_TILE_SCENE 5\n0\n2\n\"Same\"\n\"Same\"\n";
    }
    if (loadRecords(path, restored.data(), capacity) || restored != retained || worldLayers != expectedLayers)
    {
        failures++;
    }
    {
        std::ofstream f(path);
        f << "OXYGEN_TILE_SCENE 5\n1\n1\n\"Layer 1\"\n0 2 1 0 1 0 2 2 2 0 0 0 2 0 0 0 7 \"\"\n";
    }
    if (loadRecords(path, restored.data(), capacity) || restored != retained || worldLayers != expectedLayers)
    {
        failures++;
    }
    {
        std::ofstream f(path);
        f << "OXYGEN_TILE_SCENE 4\n1\n0 2 1 0 1 0 2 2 2 0 0 0 2 0 0 0 7 \"\"\n";
    }
    if (!loadRecords(path, restored.data(), capacity) || worldLayers.size() != 8 ||
        worldLayers[0] != "World" || worldLayers[7] != "Layer 8" || restored[15] != 7)
    {
        failures++;
    }
    replaceLayers(previousLayers, previousFlags);
    currentScene = previousScene;
    puts(
        failures
            ? "FAIL: custom scene layers."
            : "PASS: Layer 1 default, custom names and >8 layers, v5 persistence, invalid data isolation and v4 migration.");
    return failures;
}

EDITOR_API int ed_layer_properties_tests()
{
    if (!smoke())
    {
        return 0;
    }
    int failures = 0;
    int initial = ed_layers_snapshot(0);
    auto previousScene = currentScene;
    ed_new_scene();
    int second = ed_layer_add("Props");
    ed_layer_set_flags(second, 3);
    int before = ed_layers_snapshot(second);
    if (!ed_layer_rename(second, "  Set dressing  ") || worldLayers[second] != "Set dressing" ||
        ed_layer_visible(second) || ed_layer_editable(second) || ed_layer_rename(second, "Layer 1") ||
        ed_layer_rename(second, "  ") || ed_layer_rename(-1, "Invalid") || ed_layer_set_flags(second, 4))
    {
        failures++;
    }
    int after = ed_layers_snapshot(second);
    ed_layers_restore(before);
    if (worldLayers[second] != "Props" || worldLayerFlags(second) != 3)
    {
        failures++;
    }
    ed_layers_restore(after);
    std::vector<float> records(capacity * stride, 0), restored(records);
    records[0] = 2;
    records[5] = records[6] = records[7] = 2;
    records[11] = 1;
    records[15] = (float)second;
    auto path = root / "Scenes" / "layer-properties-regression.oxmap";
    if (!saveRecords(path, records.data(), capacity))
    {
        failures++;
    }
    ed_new_scene();
    if (!loadRecords(path, restored.data(), capacity) || restored != records || worldLayers.size() != 2 ||
        worldLayers[1] != "Set dressing" || worldLayerFlags(0) != 0 || worldLayerFlags(1) != 3)
    {
        failures++;
    }
    {
        std::ofstream f(path);
        f << "OXYGEN_TILE_SCENE 6\n0\n1\n\"Bad flags\" 9\n";
    }
    if (loadRecords(path, restored.data(), capacity) || restored != records || worldLayers.size() != 2 ||
        worldLayerFlags(1) != 3)
    {
        failures++;
    }
    {
        std::ofstream f(path);
        f << "OXYGEN_TILE_SCENE 5\n0\n2\n\"Layer 1\"\n\"Legacy\"\n";
    }
    if (!loadRecords(path, restored.data(), capacity) || worldLayers.size() != 2 || !ed_layer_visible(1) ||
        !ed_layer_editable(1))
    {
        failures++;
    }
    ed_layer_set_flags(1, 2);
    if (!ed_layer_visible(1) || ed_layer_editable(1))
    {
        failures++;
    }
    ed_layer_set_flags(1, 1);
    if (ed_layer_visible(1) || ed_layer_editable(1))
    {
        failures++;
    }
    ed_layer_remove(0);
    if (worldLayers[0] != "Legacy" || worldLayerFlags(0) != 1)
    {
        failures++;
    }
    ed_layers_restore(initial);
    ed_layers_release(initial);
    ed_layers_release(before);
    ed_layers_release(after);
    currentScene = previousScene;
    puts(
        failures
            ? "FAIL: layer properties."
            : "PASS: layer rename validation, visibility/lock, history, v6 persistence, atomic flag validation and v5 migration.");
    return failures;
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
    auto previousLayers = worldLayers;
    auto previousFlags = layerFlags;
    fs::path legacy = root / "Scenes" / "legacy-gizmo-test.oxmap";
    {
        std::ofstream f(legacy);
        f << "OXYGEN_TILE_SCENE 1\n1\n2 0 0 0 45 2 2 2 0 0 0 1 \"\"\n";
    }
    bool compatible =
        loadRecords(legacy, copy.data(), capacity) && copy[4] == 45 && copy[12] == 0 && copy[13] == 0;
    copy[1] = 20001;
    copy[2] = -20002;
    copy[3] = -20003;
    addWorldLayer("Decorations test");
    copy[15] = 1;
    copy[12] = 30;
    copy[13] = -25;
    fs::path rotated = root / "Scenes" / "rotation-gizmo-test.oxmap";
    bool saved = saveRecords(rotated, copy.data(), capacity);
    copy.assign(copy.size(), 0);
    bool restored = saved && loadRecords(rotated, copy.data(), capacity) && copy[4] == 45 && copy[12] == 30 &&
                    copy[13] == -25 && copy[15] == 1 && copy[1] == 20001 && copy[2] == -20002 &&
                    copy[3] == -20003;
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
    replaceLayers(previousLayers, previousFlags);
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
    viewCamera = c;
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
EDITOR_API int ed_grid_ray(const float *ray, const float *bounds, float *out)
{
    RayCollision hit =
        GetRayCollisionBox({{ray[0], ray[1], ray[2]}, {ray[3], ray[4], ray[5]}},
                           {{bounds[0], bounds[1], bounds[2]}, {bounds[3], bounds[4], bounds[5]}});
    if (!hit.hit || hit.distance < 0)
    {
        return 0;
    }
    out[0] = hit.point.x;
    out[1] = hit.point.y;
    out[2] = hit.point.z;
    out[3] = hit.normal.x;
    out[4] = hit.normal.y;
    out[5] = hit.normal.z;
    out[6] = hit.distance;
    return 1;
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
EDITOR_API void ed_grid(float height, float centerX, float centerZ)
{
    constexpr int radius = 32;
    constexpr float extent = radius * sceneGridSpacing;
    float cx = std::floor(centerX / sceneGridSpacing) * sceneGridSpacing;
    float cz = std::floor(centerZ / sceneGridSpacing) * sceneGridSpacing;
    for (int i = -radius; i <= radius; i++)
    {
        float x = cx + i * sceneGridSpacing;
        float z = cz + i * sceneGridSpacing;
        Color minor{65, 78, 92, 255}, axis{85, 130, 136, 255};
        DrawLine3D({x, height, cz - extent}, {x, height, cz + extent}, x == 0 ? axis : minor);
        DrawLine3D({cx - extent, height, z}, {cx + extent, height, z}, z == 0 ? axis : minor);
    }
}
EDITOR_API float ed_grid_floor(float value)
{
    return std::floor(value);
}

EDITOR_API void ed_outline(float x, float y, float z, float sx, float sy, float sz, unsigned color)
{
    DrawCubeWires({x, y + sy * .5f, z}, sx + .02f, sy + .02f, sz + .02f, GetColor(color));
}
#include "editor_workspace.inl"

EDITOR_API int ed_validate_scene(const float *records)
{
    return sceneCallback(4, nullptr, const_cast<float *>(records), capacity);
}

#include "editor_tiles.inl"

EDITOR_API void ed_bind_workflow(WorkflowCallback callback)
{
    workflowCallback = callback;
}
EDITOR_API const char *ed_application_directory()
{
    return GetApplicationDirectory();
}
EDITOR_API const char *ed_choose_dialog(int kind, const char *title, const char *initial)
{
    static char buffer[32768];
    buffer[0] = 0;
    if (kind == 0)
    {
        ed_native_folder(title, buffer, sizeof(buffer));
    }
    else
    {
        ed_native_file(kind == 2 || kind == 4, kind >= 3, initial, buffer, sizeof(buffer));
    }
    return buffer;
}
EDITOR_API int ed_dialog_guard()
{
    return ed_native_guard();
}
EDITOR_API void ed_scene_view(const char *path)
{
    currentScene = fromUtf8(path);
}
EDITOR_API void ed_checker_image(const char *path)
{
    Image image = GenImageChecked(128, 128, 16, 16, {154, 171, 184, 255}, {113, 132, 147, 255});
    ExportImage(image, path);
    UnloadImage(image);
}
EDITOR_API void ed_export_wave(short *samples, int count, int rate, const char *path)
{
    Wave wave{(unsigned)count, (unsigned)rate, 16, 1, samples};
    ExportWave(wave, path);
}

#include <cstdint>
#include <map>
#include <set>
extern "C" int ox_indexed_model(const float *, int, const unsigned short *, int, int);
extern "C" int ox_model_geometry(int, int *);
// Tile storage and GUI use the shared renderer. Mesh editing/history live in
// TileDocument.xe.
namespace
{
constexpr int tileVertexMax = 512, tileFaceMax = 512, tileFaceOffset = 2048, tileFaceStride = 13,
              tileDataSize = 8704;
using TileAssetsCallback = int (*)(int, const char *, const char *, const char *, const char *, float *);
TileAssetsCallback tileAssetsCallback = nullptr;
using TileInteractionCallback = int (*)(int, float *, float *, float *, float *, float *);
TileInteractionCallback tileInteractionCallback = nullptr;
using AtlasCallback = int (*)(int, float *, float *, float *, float *, float *, float *);
AtlasCallback atlasCallback = nullptr;
using TileViewportCallback = int (*)(int, float *, float *, float *, float *, float *);
TileViewportCallback tileViewportCallback = nullptr;
using TilePaletteCallback = int (*)(int, float *, int);
TilePaletteCallback tilePaletteCallback = nullptr;
std::vector<std::string> tileSetLabels;
using TilePanelCallback = void (*)(int, float *, float *, float *, float *, char *);
TilePanelCallback tilePanelCallback = nullptr;
bool tileSnap = true, tileWire = true;
float tileGrid = .25f;
using AtlasPanelCallback = int (*)(float *, float *, float *, float *, float *);
AtlasPanelCallback atlasPanelCallback = nullptr;
int atlasTab = 0;
using TileSessionCallback = int (*)(int, float *, float *, float *, char *, const char *);
TileSessionCallback tileSessionCallback = nullptr;
int dispatchTileSession(int operation, float *data = nullptr, float *selection = nullptr, int action = 0,
                        bool open = true);
struct TileMeta
{
    std::string name;
    fs::path texture;
};
struct TileCard
{
    fs::path relative;
    std::string name;
    RenderTexture2D image{};
    fs::path set;
};
std::vector<TileCard> tileCards;
std::vector<fs::path> tileSets;
fs::path tileCatalogRoot, tileSet, tileFile;
bool tileCatalogDirty = true, tileOpen = false, tileDirty = false, tileFocused = false;
float *tileData = nullptr;
float tileValues[16]{}, tileUV[4]{0, 0, 1, 1};
char tileName[128] = "Untitled", tilesetName[128] = "", tileError[256] = "";
fs::path tileTexture;
int tileFace = -1, tileMode = 2;
std::vector<int> tileOrder;
RenderTexture2D tileViewport{};
float tileYaw = .7f, tilePitch = .45f, tileDistance = 7;
Vector3 tileTarget{0, 1, 0};

Vector3 tilePosition(const float *data, int i)
{
    return {data[i * 4 + 1], data[i * 4 + 2], data[i * 4 + 3]};
}
bool tileValid(const float *data)
{
    return tileCodecCallback(1, nullptr, const_cast<float *>(data), nullptr, nullptr) != 0;
}
bool tileRead(const fs::path &path, float *data, TileMeta &meta)
{
    char name[128]{}, texture[32768]{};
    if (!tileCodecCallback(0, utf8(path).c_str(), data, name, texture))
    {
        return false;
    }
    meta.name = name;
    meta.texture = fromUtf8(texture);
    return true;
}
struct TileMesh
{
    std::vector<std::array<float, 8>> vertices;
    std::vector<unsigned short> indices;
    int removed = 0;
};
TileMesh tileCompile(const float *data)
{
    TileMesh mesh;
    mesh.vertices.resize(tileFaceMax * 6);
    mesh.indices.resize(tileFaceMax * 6);
    int counts[3]{};
    if (!tileMeshCallback(const_cast<float *>(data), mesh.vertices[0].data(), mesh.indices.data(), counts))
    {
        return {};
    }
    mesh.vertices.resize(counts[0]);
    mesh.indices.resize(counts[1]);
    mesh.removed = counts[2];
    return mesh;
}
// Runtime artifact contains a single indexed mesh, not per-face objects or
// reverse-side triangles.
bool tileExport(const fs::path &relative, const float *data, const TileMeta &meta)
{
    auto name = meta.name;
    auto texture = utf8(meta.texture);
    return tileCodecCallback(4, utf8(relative).c_str(), const_cast<float *>(data), name.data(),
                             texture.data()) != 0;
}
void tileClearCatalog()
{
    for (auto &card : tileCards)
    {
        if (card.image.id)
        {
            UnloadRenderTexture(card.image);
        }
    }
    tileCards.clear();
    tileSets.clear();
    tileSetLabels.clear();
    tileCatalogDirty = true;
}
#include "editor_tilesets.inl"

void tileScan()
{
    if (!tileCatalogDirty && tileCatalogRoot == root)
    {
        return;
    }
    tileCatalogRoot = root;
    tileAssetsCallback(1, utf8(tileSet).c_str(), "", "", "", nullptr);
    tileCatalogDirty = false;
}
bool tileSave()
{
    return tileAssetsCallback(0, utf8(tileSet).c_str(), utf8(tileFile).c_str(), tileName,
                              utf8(tileTexture).c_str(), tileData) != 0;
}
bool tileGuard()
{
    return dispatchTileSession(1) != 0;
}
Texture2D tileTextureImage(const fs::path &path)
{
    int id = registerAsset(path);
    return ox_lookup_texture(handle(asset(id)));
}
void tileDraw(const float *data, Texture2D texture, bool wire, int selectedFace,
              const float *selection = nullptr)
{
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);
    rlSetTexture(texture.id ? texture.id : rlGetTextureIdDefault());
    for (int i = 0; i < tileFaceMax; i++)
    {
        const float *f = data + tileFaceOffset + i * tileFaceStride;
        if (!f[0])
        {
            continue;
        }
        Vector3 p[4];
        for (int j = 0; j < 4; j++)
        {
            p[j] = tilePosition(data, (int)f[1 + j]);
        }
        Vector3 n =
            Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p[1], p[0]), Vector3Subtract(p[2], p[0])));
        float shade = .62f + .38f * fabsf(Vector3DotProduct(n, Vector3Normalize({-.4f, .8f, .5f})));
        unsigned char c = (unsigned char)(255 * shade);
        bool selected = selection ? selection[(int)f[1]] && selection[(int)f[2]] && selection[(int)f[3]] &&
                                        selection[(int)f[4]]
                                  : i == selectedFace;
        rlColor4ub(c, selected ? 255 : c, c, 255);
        Vector2 uv[4]{{f[5], f[6]}, {f[7], f[8]}, {f[9], f[10]}, {f[11], f[12]}};
        const int indices[] = {0, 1, 2, 0, 2, 3};
        for (int q : indices)
        {
            rlTexCoord2f(uv[q].x, uv[q].y);
            rlVertex3f(p[q].x, p[q].y, p[q].z);
        }
    }
    rlEnd();
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    rlEnableBackfaceCulling();
    if (wire)
    {
        for (int i = 0; i < tileFaceMax; i++)
        {
            const float *f = data + tileFaceOffset + i * tileFaceStride;
            if (!f[0])
            {
                continue;
            }
            for (int j = 0; j < 4; j++)
            {
                DrawLine3D(tilePosition(data, (int)f[j + 1]), tilePosition(data, (int)f[(j + 1) % 4 + 1]),
                           {80, 160, 180, 255});
            }
        }
    }
}
void tileFrame(const float *data, Vector3 &center, float &radius)
{
    float result[4]{};
    tileViewportCallback(2, const_cast<float *>(data), nullptr, nullptr, nullptr, result);
    center = {result[0], result[1], result[2]};
    radius = result[3];
}
std::string tileSetName(const fs::path &set)
{
    TileSetInfo info;
    return tileSetRead(set, info) ? info.name : utf8(set.filename());
}
void tilePalette(const float *state)
{
    tilePaletteCallback(0, const_cast<float *>(state), 0);
}
} // namespace

EDITOR_API int ed_tile_guard()
{
    return tileGuard();
}
EDITOR_API void ed_tile_reset()
{
    tileOpen = false;
    tileDirty = false;
    tileFile.clear();
    tileTexture.clear();
    tileSet.clear();
    paletteSelection.clear();
    paletteSelectionSet.clear();
    tileSessionCallback(2, nullptr, nullptr, nullptr, nullptr, nullptr);
    tileFocused = false;
    tileClearCatalog();
}
EDITOR_API void ed_tile_close()
{
    ed_tile_reset();
    if (tileViewport.id)
    {
        UnloadRenderTexture(tileViewport);
        tileViewport = {};
    }
}
EDITOR_API void ed_tile_values(float *values)
{
    std::copy(tileValues, tileValues + 16, values);
}
int tileLoadModel(const fs::path &relative)
{
    return tileAssetsCallback(3, "", utf8(relative).c_str(), "", "", nullptr);
}

namespace
{
int atlasCell[2]{16, 16}, placementPlane = 0;
float placementDepth = 0, cellMetres = sceneGridSpacing, atlasZoom = 1;
bool atlasSelecting = false, atlasDragging = false, atlasDragArmed = false, atlasHasSelection = false;
Vector2 atlasStart{}, atlasMouseStart{};
float faceUVEdit[8]{};
int uvCorner = -1;
bool uvGesture = false;

#include "editor_tile_gizmo.inl"
#include "editor_tile_selection.inl"

bool tileDuplicatePending = false;
int tileFirstSelectedFace(const float *data, const float *selection)
{
    float view[1]{};
    tilePanelCallback(2, const_cast<float *>(data), const_cast<float *>(selection), view, nullptr, nullptr);
    return (int)view[0];
}

int dispatchAtlas(int operation, const float *data = nullptr, float *input = nullptr)
{
    float display[6]{};
    int action =
        atlasCallback(operation, const_cast<float *>(data), tileUV, faceUVEdit, input, tileValues, display);
    atlasSelecting = display[0] != 0;
    atlasDragging = display[1] != 0;
    atlasDragArmed = display[2] != 0;
    atlasHasSelection = display[3] != 0;
    uvGesture = display[4] != 0;
    uvCorner = (int)display[5];
    return action;
}
void atlasReset()
{
    dispatchAtlas(1);
    resetTileHandle();
    tileBox = {};
    tileDuplicatePending = false;
    tileUV[0] = tileUV[1] = 0;
    tileUV[2] = tileUV[3] = 1;
}
// A region spanning N x M cells creates one face, with a proportional physical
// size.
void tileAtlas(const float *data, Texture2D texture, int &action)
{
    float settings[]{(float)atlasTab,   (float)atlasCell[0],  (float)atlasCell[1],   atlasZoom,
                     (float)texture.id, (float)texture.width, (float)texture.height, (float)tileFace,
                     cellMetres};
    int result = atlasPanelCallback(const_cast<float *>(data), tileUV, faceUVEdit, settings, tileValues);
    atlasTab = (int)settings[0];
    atlasCell[0] = (int)settings[1];
    atlasCell[1] = (int)settings[2];
    atlasZoom = settings[3];
    if (result)
    {
        action = result;
    }
    dispatchAtlas(6);
}
} // namespace

#include "editor_tile_gizmo_tests.inl"

void tilePanelControls(int part, float *data, float *selection, int cursor, int count, int &action)
{
    Texture2D texture = tileTexture.empty() ? Texture2D{} : tileTextureImage(tileTexture);
    float view[]{(float)tileFace,
                 (float)tileMode,
                 (float)tileTool,
                 tileFocused ? 1.f : 0.f,
                 tileHandle.active ? 1.f : 0.f,
                 tileBox.active ? 1.f : 0.f,
                 uvGesture ? 1.f : 0.f,
                 atlasDragging ? 1.f : 0.f,
                 atlasSelecting ? 1.f : 0.f,
                 tileDirty ? 1.f : 0.f,
                 tileDuplicatePending ? 1.f : 0.f,
                 (float)placementPlane,
                 placementDepth,
                 cellMetres,
                 tileSnap ? 1.f : 0.f,
                 tileGrid,
                 tileWire ? 1.f : 0.f,
                 (float)texture.id,
                 (float)texture.width,
                 (float)texture.height,
                 (float)action,
                 (float)cursor,
                 (float)count};
    tilePanelCallback(part, data, selection, view, tileValues, tileName);
    tileFace = (int)view[0];
    tileMode = (int)view[1];
    tileTool = (int)view[2];
    tileDirty = view[9] != 0;
    tileDuplicatePending = view[10] != 0;
    placementPlane = (int)view[11];
    placementDepth = view[12];
    cellMetres = view[13];
    tileSnap = view[14] != 0;
    tileGrid = view[15];
    tileWire = view[16] != 0;
    action = (int)view[20];
    if (action == 7 || action == 27)
    {
        tileOrder.clear();
    }
}

namespace
{
int dispatchTileSession(int operation, float *data, float *selection, int action, bool open)
{
    float state[]{tileOpen ? 1.f : 0.f,
                  tileDirty ? 1.f : 0.f,
                  tileFocused ? 1.f : 0.f,
                  (float)tileFace,
                  (float)tileMode,
                  (float)tileTool,
                  tileDuplicatePending ? 1.f : 0.f,
                  tileYaw,
                  tilePitch,
                  tileDistance,
                  tileTarget.x,
                  tileTarget.y,
                  tileTarget.z,
                  (float)action,
                  open ? 1.f : 0.f,
                  (ImGui::IsMouseReleased(0) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ? 1.f : 0.f};
    bool duplicate = tileDuplicatePending;
    int result = tileSessionCallback(operation, data, selection, state, tileName, nullptr);
    tileOpen = state[0] != 0;
    tileDirty = state[1] != 0;
    tileFocused = state[2] != 0;
    tileFace = (int)state[3];
    tileMode = (int)state[4];
    tileTool = (int)state[5];
    tileDuplicatePending = state[6] != 0;
    tileYaw = state[7];
    tilePitch = state[8];
    tileDistance = state[9];
    tileTarget = {state[10], state[11], state[12]};
    if (operation == 0 && duplicate)
    {
        tileSelectionOrder(selection);
    }
    if (operation == 6)
    {
        dispatchAtlas(6);
    }
    return result;
}
} // namespace

EDITOR_API int ed_tile_ui(float *data, float *selection, int cursor, int count)
{
    tileData = data;
    std::fill(std::begin(tileValues), std::end(tileValues), 0.f);
    int action = dispatchTileSession(0, data, selection);
    if (!tileOpen)
    {
        tileBox = {};
        return action;
    }
    bool open = true;
    ImGui::SetNextWindowSize({1100, 760}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(tileDirty ? "Tile Editor *###TileEditor" : "Tile Editor###TileEditor", &open))
    {
        tileFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        tilePanelControls(0, data, selection, cursor, count, action);
        Texture2D texture = tileTexture.empty() ? Texture2D{} : tileTextureImage(tileTexture);
        ImGui::SameLine();
        ImGui::BeginChild("Tile viewport", {0, 0}, ImGuiChildFlags_Borders);
        tilePanelControls(1, data, selection, cursor, count, action);
        ImVec2 size = ImGui::GetContentRegionAvail(), origin = ImGui::GetCursorScreenPos();
        int width = std::max(1, (int)size.x), height = std::max(1, (int)size.y);
        if (tileViewport.texture.width != width || tileViewport.texture.height != height)
        {
            if (tileViewport.id)
            {
                UnloadRenderTexture(tileViewport);
            }
            tileViewport = LoadRenderTexture(width, height);
        }
        ImGui::Image((ImTextureID)tileViewport.texture.id, {(float)width, (float)height}, {0, 1}, {1, 0});
        bool hovered = ImGui::IsItemHovered();
        ImGuiIO &io = ImGui::GetIO();
        float cameraState[]{tileYaw, tilePitch, tileDistance, tileTarget.x, tileTarget.y, tileTarget.z};
        float cameraInput[]{hovered ? 1.f : 0.f,
                            (tileHandle.active || tileBox.active) ? 1.f : 0.f,
                            ImGui::IsMouseDown(1) ? 1.f : 0.f,
                            ImGui::IsMouseDown(2) ? 1.f : 0.f,
                            io.MouseDelta.x,
                            io.MouseDelta.y,
                            io.MouseWheel,
                            ImGui::IsKeyPressed(ImGuiKey_F) ? 1.f : 0.f,
                            (float)width,
                            (float)height};
        float cameraOutput[14]{};
        tileViewportCallback(0, data, cameraState, cameraInput, nullptr, cameraOutput);
        tileYaw = cameraState[0];
        tilePitch = cameraState[1];
        tileDistance = cameraState[2];
        tileTarget = {cameraState[3], cameraState[4], cameraState[5]};
        Camera3D camera{{cameraOutput[0], cameraOutput[1], cameraOutput[2]},
                        {cameraOutput[3], cameraOutput[4], cameraOutput[5]},
                        {0, 1, 0},
                        cameraOutput[9],
                        CAMERA_PERSPECTIVE};
        Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        Vector2 mouse{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
        Ray ray = GetScreenToWorldRayEx(mouse, camera, width, height);
        float rayData[]{ray.position.x,  ray.position.y,  ray.position.z,
                        ray.direction.x, ray.direction.y, ray.direction.z};
        float dropInput[]{mouse.x,
                          mouse.y,
                          (float)width,
                          (float)height,
                          (float)placementPlane,
                          placementDepth,
                          tileSnap ? 1.f : 0.f,
                          tileGrid,
                          (float)texture.width,
                          (float)texture.height,
                          (float)atlasCell[0],
                          (float)atlasCell[1],
                          cellMetres,
                          texture.id ? 1.f : 0.f,
                          atlasDragging ? 1.f : 0.f,
                          ImGui::IsKeyPressed(ImGuiKey_Escape) ? 1.f : 0.f,
                          io.AppFocusLost ? 1.f : 0.f,
                          ImGui::IsMouseReleased(0) ? 1.f : 0.f};
        float previewValues[16]{};
        int placement = tileViewportCallback(1, data, rayData, dropInput, tileUV, previewValues);
        bool showPreview = placement != 0;
        std::array<float, tileDataSize> previewData{};
        if (showPreview)
        {
            tilePreviewData(previewData.data(), {previewValues[0], previewValues[1], previewValues[2]},
                            {previewValues[3], previewValues[4]}, (int)previewValues[5], previewValues + 8);
        }
        if (placement == 2)
        {
            action = 2;
            std::copy(previewValues, previewValues + 16, tileValues);
            if (previewValues[6] >= 0)
            {
                tileFace = (int)previewValues[6];
            }
            tileMode = 2;
        }
        bool gizmoConsumed =
            !tileBox.active && tileGizmo(data, selection, camera, origin, width, height, hovered,
                                         atlasDragging || atlasSelecting || uvGesture || action != 0,
                                         tileSnap, tileGrid, action);
        float selectionCamera[]{camera.position.x,
                                camera.position.y,
                                camera.position.z,
                                camera.target.x,
                                camera.target.y,
                                camera.target.z,
                                camera.up.x,
                                camera.up.y,
                                camera.up.z,
                                camera.fovy,
                                0,
                                0,
                                (float)width,
                                (float)height};
        float selectionInput[]{mouse.x,
                               mouse.y,
                               (float)tileMode,
                               (float)tileFace,
                               hovered ? 1.f : 0.f,
                               gizmoConsumed ? 1.f : 0.f,
                               atlasDragging ? 1.f : 0.f,
                               atlasSelecting ? 1.f : 0.f,
                               ImGui::IsMouseClicked(0) ? 1.f : 0.f,
                               io.KeyCtrl ? 1.f : 0.f,
                               io.MouseDragThreshold,
                               ImGui::IsMouseDown(0) ? 1.f : 0.f,
                               (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(1) ||
                                !tileFocused || io.AppFocusLost)
                                   ? 1.f
                                   : 0.f};
        float selectionView[5]{};
        tileInteractionCallback(4, data, selection, selectionCamera, selectionInput, selectionView);
        tileFace = (int)selectionView[0];
        tileBox = {selectionView[1] != 0, selectionView[2] != 0, {selectionView[3], selectionView[4]}};
        tileSelectionOrder(selection);
        if (tileBox.active && tileBox.dragged)
        {
            tileBoxDraw(origin, width, height, mouse);
        }
        BeginTextureMode(tileViewport);
        ClearBackground({24, 30, 39, 255});
        BeginMode3D(camera);
        ed_grid(0, camera.position.x, camera.position.z);
        tileDraw(data, tileTexture.empty() ? Texture2D{} : tileTextureImage(tileTexture), tileWire, tileFace,
                 tileMode == 2 ? selection : nullptr);
        if (showPreview)
        {
            tileDraw(previewData.data(), texture, true, -1);
        }
        EndMode3D();
        if (tileMode == 1)
        {
            for (int i = 0; i < tileVertexMax; i++)
            {
                if (!data[i * 4])
                {
                    continue;
                }
                Vector3 p = tilePosition(data, i);
                if (Vector3DotProduct(Vector3Subtract(p, camera.position), forward) <= 0)
                {
                    continue;
                }
                Vector2 screen = GetWorldToScreenEx(p, camera, width, height);
                DrawCircleV(screen, selection[i] ? 5.f : 3.f,
                            selection[i] ? Color{255, 190, 70, 255} : Color{170, 210, 225, 255});
            }
        }
        EndTextureMode();
        ImGui::EndChild();
    }
    ImGui::End();
    dispatchTileSession(6, tileValues, nullptr, action, open);
    return action;
}

#include "editor_model_tests.inl"
#include "editor_tile_keyboard_tests.inl"
#include "editor_tilesets_tests.inl"

EDITOR_API int ed_tile_tests()
{
    std::array<float, tileDataSize> data{}, loaded{};
    float points[12]{-1, 0, 0, 1, 0, 0, 1, 2, 0, -1, 2, 0};
    for (int i = 0; i < 4; i++)
    {
        data[i * 4] = 1;
        for (int j = 0; j < 3; j++)
        {
            data[i * 4 + 1 + j] = points[i * 3 + j];
        }
    }
    data[tileFaceOffset] = 1;
    for (int i = 0; i < 4; i++)
    {
        data[tileFaceOffset + 1 + i] = (float)i;
    }
    float initialUV[8]{0, 1, 1, 1, 1, 0, 0, 0};
    std::copy(initialUV, initialUV + 8, data.data() + tileFaceOffset + 5);
    tileData = data.data();
    tileSet = fs::path("Tiles") / "tile_Regression";
    tileFile.clear();
    tileTexture.clear();
    fs::create_directories(root / "Assets" / tileSet);
    {
        std::ofstream f(root / "Assets" / tileSet / "tileset.oxtileset");
        f << "OXYGEN_TILESET 1\n\"Regression\"\n";
    }
    snprintf(tileName, sizeof(tileName), "Plane");
    int failures = tileHandleTests(data.data()) + tileBoxTests(data.data());
    if (!tileSave())
    {
        failures++;
    }
    TileMeta meta;
    if (!tileRead(root / "Assets" / tileFile, loaded.data(), meta) || loaded != data || meta.name != "Plane")
    {
        failures++;
    }
    int model = tileLoadModel(tileFile);
    if (!model)
    {
        failures++;
    }
    else
    {
        int counts[5]{};
        if (!ox_model_geometry(model, counts) || counts[0] != 1 || counts[1] != 1 || counts[2] != 4 ||
            counts[3] != 2 || counts[4] != 1)
        {
            failures++;
        }
        failures += tileModelTests(model);
        ox_release_model(model);
    }
    TileMesh compiled = tileCompile(data.data());
    if (compiled.vertices.size() != 4 || compiled.indices.size() != 6)
    {
        failures++;
    }
    auto duplicate = data;
    std::copy(data.data() + tileFaceOffset, data.data() + tileFaceOffset + tileFaceStride,
              duplicate.data() + tileFaceOffset + tileFaceStride);
    compiled = tileCompile(duplicate.data());
    if (compiled.vertices.size() != 4 || compiled.indices.size() != 6 || compiled.removed != 2)
    {
        failures++;
    }
    duplicate[tileFaceOffset + tileFaceStride + 5] = .25f;
    compiled = tileCompile(duplicate.data());
    if (compiled.indices.size() != 12 || compiled.vertices.size() != 5)
    {
        failures++;
    }
    // Neighbouring quads share an edge and continuous UVs: six vertices, four
    // triangles.
    auto adjacent = data;
    adjacent[tileFaceOffset + 7] = adjacent[tileFaceOffset + 9] = .5f;
    for (int i = 0; i < 4; i++)
    {
        std::copy(data.data() + i * 4, data.data() + i * 4 + 4, adjacent.data() + (i + 4) * 4);
        adjacent[(i + 4) * 4 + 1] += 2;
    }
    float *second = adjacent.data() + tileFaceOffset + tileFaceStride;
    std::copy(adjacent.data() + tileFaceOffset, adjacent.data() + tileFaceOffset + tileFaceStride, second);
    for (int i = 1; i <= 4; i++)
    {
        second[i] += 4;
    }
    for (int i = 0; i < 4; i++)
    {
        second[5 + i * 2] += .5f;
    }
    compiled = tileCompile(adjacent.data());
    if (compiled.vertices.size() != 6 || compiled.indices.size() != 12)
    {
        failures++;
    }
    fs::path legacy = root / "Assets" / tileSet / "legacy.oxtile";
    {
        std::ofstream f(legacy);
        f << "OXYGEN_TILE 1\n\"Legacy\"\n\"\"\n4 1\n0 -1 0 0\n1 1 0 0\n2 1 2 0\n3 "
             "-1 2 0\n0 0 1 2 3 0 0 1 1\n";
    }
    registerAsset(legacy.lexically_relative(root / "Assets"));
    // Volumetric two-metre fixture and the four-metre pair of walls from the grid regression.
    for (int size : {2, 4})
    {
        fs::path path = root / "Assets" / tileSet / (size == 2 ? "grid-unit.oxtile" : "grid-wide.oxtile");
        std::ofstream f(path);
        f << "OXYGEN_TILE 1\n\"Grid\"\n\"\"\n8 2\n";
        f << "0 " << -size << " 0 0\n1 0 0 0\n2 0 " << size << " 0\n3 " << -size << " " << size << " 0\n";
        f << "4 " << -size << " 0 " << -size << "\n5 0 0 " << -size << "\n6 0 " << size << " " << -size
          << "\n7 " << -size << " " << size << " " << -size << "\n";
        f << "0 0 1 2 3 0 0 1 1\n1 4 5 6 7 0 0 1 1\n";
        f.close();
        registerAsset(path.lexically_relative(root / "Assets"));
    }

    fs::path fractional = root / "Assets" / tileSet / "grid-fractional.oxtile";
    {
        std::ofstream f(fractional);
        f << "OXYGEN_TILE 1\n\"Fractional walls\"\n\"\"\n8 2\n"
             "0 -4 0 0\n1 0 0 0\n2 0 4 0\n3 -4 4 0\n"
             "4 -2.75 0 -4\n5 1.25 0 -4\n6 1.25 4 -4\n7 -2.75 4 -4\n"
             "0 0 1 2 3 0 0 1 1\n1 4 5 6 7 0 0 1 1\n";
    }
    registerAsset(fractional.lexically_relative(root / "Assets"));

    if (!tileRead(legacy, loaded.data(), meta) || loaded != data)
    {
        failures++;
    }
    fs::path bad = root / "Assets" / tileSet / "broken.oxtile";
    {
        std::ofstream f(bad);
        f << "OXYGEN_TILE 1\n\"Bad\"\n\"../../outside.png\"\n0 0\n";
    }
    if (tileRead(bad, loaded.data(), meta) || loaded != data)
    {
        failures++;
    }
    {
        std::ofstream f(bad);
        f << "OXYGEN_TILE 1\n\"Bad\"\n\"\"\n513 0\n";
    }
    if (tileRead(bad, loaded.data(), meta))
    {
        failures++;
    }
    // Regression: switching from grid lines to triangles must preserve the tile
    // texture.
    Image solid = GenImageColor(4, 4, {255, 0, 0, 255});
    Texture2D colored = LoadTextureFromImage(solid);
    UnloadImage(solid);
    RenderTexture2D target = LoadRenderTexture(48, 48);
    for (int side = 0; side < 2; side++)
    {
        Camera3D camera{{0, 1, side == 0 ? 4.f : -4.f}, {0, 1, 0}, {0, 1, 0}, 45, CAMERA_PERSPECTIVE};
        BeginTextureMode(target);
        ClearBackground(BLACK);
        BeginMode3D(camera);
        DrawGrid(4, 1);
        tileDraw(data.data(), colored, false, -1);
        EndMode3D();
        EndTextureMode();
        Image capture = LoadImageFromTexture(target.texture);
        Color pixel = GetImageColor(capture, 24, 24);
        if (pixel.r < 100 || pixel.g > 10 || pixel.b > 10)
        {
            failures++;
        }
        UnloadImage(capture);
    }
    UnloadRenderTexture(target);
    UnloadTexture(colored);
    failures += tileSetTests(tileFile);
    failures += tileKeyboardTests(data.data(), tileFile);
    // Empty editable tiles are valid after deleting all faces; loading them as a
    // model must safely return no geometry, not dereference an empty vertex array.
    auto full = data;
    std::fill(data.begin(), data.end(), 0.f);
    if (!tileSave() || !tileRead(root / "Assets" / tileFile, loaded.data(), meta) || loaded != data ||
        tileLoadModel(tileFile) != 0)
    {
        failures++;
    }
    data = full;
    data[tileFaceOffset + 1] = 511;
    if (tileValid(data.data()))
    {
        failures++;
    }
    ed_tile_reset();
    tileData = nullptr;
    puts(failures ? "FAIL: tile storage/export validation."
                  : "PASS: tile v1/v2 UV roundtrip, one indexed GPU mesh, welding, UV "
                    "seams, duplicate removal and textured previews.");
    return failures;
}

EDITOR_API int ed_tile_suspend()
{
    return dispatchTileSession(7);
}

EDITOR_API void ed_tileset_view(int operation, const char *text)
{
    if (operation == 0 && tileSetResult)
    {
        tileSetResult->name = text;
    }
    if (operation == 1 && tileSetResult)
    {
        tileSetResult->members.push_back(fromUtf8(text));
    }
    if (operation == 2)
    {
        tileSet = fromUtf8(text);
    }
}

EDITOR_API void ed_bind_tile_gizmo(TileGizmoCallback callback)
{
    tileGizmoCallback = callback;
}

EDITOR_API void ed_bind_tile_assets(TileAssetsCallback callback)
{
    tileAssetsCallback = callback;
}
EDITOR_API void ed_tile_catalog_view(int operation, const char *path, const char *name, const char *set)
{
    if (operation == 0)
    {
        tileClearCatalog();
    }
    if (operation == 1)
    {
        tileSets.push_back(fromUtf8(path));
        tileSetLabels.push_back(name);
    }
    if (operation == 2)
    {
        tileCards.push_back({fromUtf8(path), name, {}, fromUtf8(set)});
    }
}
EDITOR_API void ed_tile_saved_view(const char *path, int id)
{
    tileFile = fromUtf8(path);
    tileDirty = false;
    Asset *a = asset(id);
    if (a && a->handle)
    {
        ox_release_model(a->handle);
        a->handle = 0;
    }
    if (a)
    {
        a->failed = false;
    }
    tileCatalogDirty = true;
}
EDITOR_API void ed_tile_error_view(const char *text)
{
    snprintf(tileError, sizeof(tileError), "%s", text);
}

EDITOR_API void ed_bind_tile_interaction(TileInteractionCallback callback)
{
    tileInteractionCallback = callback;
}
EDITOR_API float ed_triangle_hit(float *values, float *points)
{
    Ray ray{{values[0], values[1], values[2]}, {values[3], values[4], values[5]}};
    RayCollision hit =
        GetRayCollisionTriangle(ray, {points[0], points[1], points[2]}, {points[3], points[4], points[5]},
                                {points[6], points[7], points[8]});
    return hit.hit ? hit.distance : -1.f;
}

EDITOR_API void ed_bind_atlas(AtlasCallback callback)
{
    atlasCallback = callback;
}

EDITOR_API void ed_bind_tile_viewport(TileViewportCallback callback)
{
    tileViewportCallback = callback;
}

EDITOR_API void ed_tile_resource_request(int id)
{
    Asset *a = asset(id);
    if (a)
    {
        tileSessionCallback(5, nullptr, nullptr, nullptr, nullptr, utf8(a->relative).c_str());
    }
}

EDITOR_API void ed_bind_tile_palette(TilePaletteCallback callback)
{
    tilePaletteCallback = callback;
}
EDITOR_API int ed_tile_catalog_count(int kind)
{
    return kind == 0 ? (int)tileSets.size() : (int)tileCards.size();
}
EDITOR_API const char *ed_tile_catalog_text(int kind, int index)
{
    static std::string text;
    text.clear();
    if (kind == 0 && index >= 0 && index < (int)tileSets.size())
    {
        text = utf8(tileSets[index]);
    }
    if (kind == 1 && index >= 0 && index < (int)tileSetLabels.size())
    {
        text = tileSetLabels[index];
    }
    if (index >= 0 && index < (int)tileCards.size())
    {
        if (kind == 2)
        {
            text = utf8(tileCards[index].relative);
        }
        if (kind == 3)
        {
            text = tileCards[index].name;
        }
        if (kind == 4)
        {
            text = utf8(tileCards[index].set);
        }
    }
    if (kind == 5)
    {
        text = utf8(tileSet);
    }
    if (kind == 6)
    {
        text = tileError;
    }
    return text.c_str();
}
EDITOR_API void ed_tile_catalog_scan()
{
    tileScan();
}
EDITOR_API void ed_tile_new_request()
{
    tileSessionCallback(3, nullptr, nullptr, nullptr, nullptr, nullptr);
}
EDITOR_API void ed_tile_open_request(const char *path)
{
    tileSessionCallback(4, nullptr, nullptr, nullptr, nullptr, path);
}
EDITOR_API void ed_tile_selection_view(int operation, const char *path)
{
    if (operation == 0)
    {
        paletteSelection.clear();
        paletteSelectionSet = fromUtf8(path);
    }
    if (operation == 1)
    {
        paletteSelection.push_back(fromUtf8(path));
    }
    if (operation == 2)
    {
        tileCatalogDirty = true;
    }
}
EDITOR_API void ed_tile_drop_area()
{
    tileSetDropTarget();
}
EDITOR_API unsigned ed_tile_thumbnail(int index, int create)
{
    if (index < 0 || index >= (int)tileCards.size())
    {
        return 0;
    }
    auto &card = tileCards[index];
    if (!card.image.id && create)
    {
        std::array<float, tileDataSize> data{};
        TileMeta meta;
        if (tileRead(root / "Assets" / card.relative, data.data(), meta))
        {
            card.image = LoadRenderTexture(128, 128);
            Vector3 center;
            float radius;
            tileFrame(data.data(), center, radius);
            Camera3D camera{Vector3Add(center, {radius * .8f, radius * .6f, radius * .8f}),
                            center,
                            {0, 1, 0},
                            40,
                            CAMERA_PERSPECTIVE};
            Texture2D texture = meta.texture.empty() ? Texture2D{} : tileTextureImage(meta.texture);
            BeginTextureMode(card.image);
            ClearBackground({30, 37, 47, 255});
            BeginMode3D(camera);
            tileDraw(data.data(), texture, false, -1);
            EndMode3D();
            EndTextureMode();
        }
    }
    return card.image.texture.id;
}

EDITOR_API void ed_bind_tile_panel(TilePanelCallback callback)
{
    tilePanelCallback = callback;
}
EDITOR_API int ui_icon(const char *label, int icon, int selected)
{
    return tileIcon(label, (TileIcon)icon, selected != 0);
}
EDITOR_API const char *ed_tile_panel_text(int kind)
{
    static std::string text;
    text.clear();
    if (kind == 0 && !tileSet.empty())
    {
        text = tileSetName(tileSet);
    }
    if (kind == 1)
    {
        text = utf8(tileTexture);
    }
    if (kind == 2)
    {
        text = utf8(tileTexture.filename());
    }
    if (kind == 3)
    {
        text = utf8(tileFile);
    }
    if (kind == 4)
    {
        text = utf8(tileSet);
    }
    return text.c_str();
}
EDITOR_API void ed_tile_panel_texture(const char *path)
{
    tileTexture = fromUtf8(path);
    atlasReset();
}
EDITOR_API int ed_tile_panel_action(int action)
{
    if (action == 0)
    {
        return tileSave();
    }
    if (action == 1)
    {
        tileFrame(tileData, tileTarget, tileDistance);
        tileDistance *= 1.6f;
    }
    return 1;
}
EDITOR_API int ed_tile_atlas_draw(float *data, float *view)
{
    tileFace = (int)view[0];
    Texture2D texture = tileTexture.empty() ? Texture2D{} : tileTextureImage(tileTexture);
    int action = 0;
    tileAtlas(data, texture, action);
    view[6] = uvGesture ? 1.f : 0.f;
    view[7] = atlasDragging ? 1.f : 0.f;
    view[8] = atlasSelecting ? 1.f : 0.f;
    view[17] = (float)texture.id;
    view[18] = (float)texture.width;
    view[19] = (float)texture.height;
    return action;
}

EDITOR_API void ed_bind_atlas_panel(AtlasPanelCallback callback)
{
    atlasPanelCallback = callback;
}
EDITOR_API int ed_atlas_canvas(float *data, float *settings)
{
    atlasTab = (int)settings[0];
    atlasCell[0] = (int)settings[1];
    atlasCell[1] = (int)settings[2];
    atlasZoom = settings[3];
    Texture2D texture = tileTextureImage(tileTexture);
    bool hasFace = tileFace >= 0 && data[tileFaceOffset + tileFace * tileFaceStride] != 0;
    int action = 0;
    ImGui::BeginChild("Atlas canvas", {0, 300}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    float base = std::max(40.f, ImGui::GetContentRegionAvail().x), width = base * atlasZoom,
          height = width * texture.height / texture.width;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)texture.id, {width, height});
    bool hovered = ImGui::IsItemHovered();
    ImGuiIO &io = ImGui::GetIO();
    Vector2 point{std::clamp((io.MousePos.x - origin.x) / width, 0.f, 1.f),
                  std::clamp((io.MousePos.y - origin.y) / height, 0.f, 1.f)};
    float input[]{(float)atlasTab,
                  hasFace ? 1.f : 0.f,
                  hovered ? 1.f : 0.f,
                  ImGui::IsMouseClicked(0) ? 1.f : 0.f,
                  ImGui::IsMouseReleased(0) ? 1.f : 0.f,
                  ImGui::IsMouseDragging(0) ? 1.f : 0.f,
                  ImGui::IsKeyPressed(ImGuiKey_Escape) ? 1.f : 0.f,
                  io.KeyShift ? 1.f : 0.f,
                  point.x,
                  point.y,
                  (float)texture.width,
                  (float)texture.height,
                  width,
                  height,
                  (float)atlasCell[0],
                  (float)atlasCell[1],
                  (float)tileFace};
    int atlasAction = dispatchAtlas(0, data, input);
    if (atlasAction)
    {
        action = atlasAction;
    }
    ImDrawList *draw = ImGui::GetWindowDrawList();
    if (atlasTab == 0)
    {
        if (width * atlasCell[0] / texture.width >= 8 && height * atlasCell[1] / texture.height >= 8)
        {
            for (int x = 0; x <= texture.width; x += atlasCell[0])
            {
                float px = origin.x + (float)x / texture.width * width;
                draw->AddLine({px, origin.y}, {px, origin.y + height}, IM_COL32(0, 0, 0, 65));
            }
            for (int y = 0; y <= texture.height; y += atlasCell[1])
            {
                float py = origin.y + (float)y / texture.height * height;
                draw->AddLine({origin.x, py}, {origin.x + width, py}, IM_COL32(0, 0, 0, 65));
            }
        }
        if (atlasHasSelection)
        {
            ImVec2 low{origin.x + tileUV[0] * width, origin.y + tileUV[1] * height},
                high{origin.x + tileUV[2] * width, origin.y + tileUV[3] * height};
            draw->AddRectFilled(low, high, IM_COL32(255, 200, 50, 40));
            draw->AddRect(low, high, IM_COL32(255, 200, 50, 255), 0, 0, 2);
        }
    }
    else if (hasFace)
    {
        for (int i = 0; i < 4; i++)
        {
            int next = (i + 1) % 4;
            ImVec2 a{origin.x + faceUVEdit[i * 2] * width, origin.y + faceUVEdit[i * 2 + 1] * height},
                b{origin.x + faceUVEdit[next * 2] * width, origin.y + faceUVEdit[next * 2 + 1] * height};
            draw->AddLine(a, b, IM_COL32(255, 220, 90, 255), 2);
            draw->AddCircleFilled(a, 5, IM_COL32(255, 240, 200, 255));
            draw->AddText({a.x + 6, a.y}, IM_COL32(255, 255, 255, 255), TextFormat("%d", i + 1));
        }
    }
    ImGui::EndChild();
    return action;
}

EDITOR_API void ed_bind_tile_session(TileSessionCallback callback)
{
    tileSessionCallback = callback;
}
EDITOR_API void ed_tile_document_view(int field, const char *value)
{
    if (field == 0)
    {
        tileFile = fromUtf8(value);
    }
    if (field == 1)
    {
        tileTexture = fromUtf8(value);
    }
}
EDITOR_API void ed_tile_reset_gestures()
{
    atlasReset();
    tileOrder.clear();
}

EDITOR_API int ed_indexed_model(float *vertices, int vertexCount, unsigned short *indices, int indexCount,
                                int texture)
{
    return ox_indexed_model(vertices, vertexCount, indices, indexCount, texture);
}

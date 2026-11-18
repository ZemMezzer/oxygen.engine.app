bool resourcePanelFocused = false, tilePaletteFocused = false, layersPanelFocused = false;
// Docking and platform input stay here; document commands and history belong to Xenon.
namespace
{
GLFWwindow *mainWindow = nullptr, *sceneWindow = nullptr, *capturedWindow = nullptr;
constexpr int panelCount = 6;
bool panelOpen[panelCount] = {true, true, true, true, true, true};
const char *panelNames[] = {"Scene", "Hierarchy", "Inspector", "Resources", "Tiles", "Layers"};
bool sceneHovered = false, sceneVisible = false, resetDock = false, transformEditing = false;
using InspectorCallback = int (*)(int, float *, float *, float *);
InspectorCallback inspectorCallback = nullptr;
using HierarchyCallback = void (*)(float *, float *);
HierarchyCallback hierarchyCallback = nullptr;
int parentSource = -1, parentTarget = -1;

using WorkspaceModelCallback = int (*)(int, int, float *, float *, char *);
WorkspaceModelCallback workspaceModelCallback = nullptr;
struct CommandQueueView
{
    void push_back(int action)
    {
        workspaceModelCallback(0, action, nullptr, nullptr, nullptr);
    }
    void push_front(int action)
    {
        workspaceModelCallback(1, action, nullptr, nullptr, nullptr);
    }
    bool empty() const
    {
        return workspaceModelCallback(2, 0, nullptr, nullptr, nullptr) == 0;
    }
    int front() const
    {
        return workspaceModelCallback(3, 0, nullptr, nullptr, nullptr);
    }
    void pop_front()
    {
        workspaceModelCallback(4, 0, nullptr, nullptr, nullptr);
    }
    void clear()
    {
        workspaceModelCallback(5, 0, nullptr, nullptr, nullptr);
    }
} commands;
std::string iniPath;
Vector2 lookDelta{};
double lastMouseX = 0, lastMouseY = 0;
bool captureFirst = true;

ImGuiKey keyFor(int key)
{
    if (key >= KEY_A && key <= KEY_Z)
    {
        return (ImGuiKey)(ImGuiKey_A + key - KEY_A);
    }
    if (key >= KEY_ZERO && key <= KEY_NINE)
    {
        return (ImGuiKey)(ImGuiKey_0 + key - KEY_ZERO);
    }
    if (key >= KEY_F1 && key <= KEY_F12)
    {
        return (ImGuiKey)(ImGuiKey_F1 + key - KEY_F1);
    }
    switch (key)
    {
    case KEY_ESCAPE:
        return ImGuiKey_Escape;
    case KEY_SPACE:
        return ImGuiKey_Space;
    case KEY_DELETE:
        return ImGuiKey_Delete;
    case KEY_LEFT_BRACKET:
        return ImGuiKey_LeftBracket;
    case KEY_RIGHT_BRACKET:
        return ImGuiKey_RightBracket;
    case KEY_LEFT_SHIFT:
        return ImGuiKey_LeftShift;
    case KEY_RIGHT_SHIFT:
        return ImGuiKey_RightShift;
    default:
        return ImGuiKey_None;
    }
}

void request(int action)
{
    commands.push_back(action);
}

void defaultDock(ImGuiID dock)
{
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
    ImGuiID center = dock, left, right, bottom, tiles, layers;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, .18f, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, .25f, &right, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, .26f, &bottom, &center);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, .43f, &tiles, &left);
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, .36f, &layers, &right);
    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Resources", bottom);
    ImGui::DockBuilderDockWindow("Tiles", tiles);
    ImGui::DockBuilderDockWindow("Layers", layers);
    ImGui::DockBuilderFinish(dock);
    std::fill(std::begin(panelOpen), std::end(panelOpen), true);
}

void ensureViewport()
{
    int w = std::max(1, (int)view.width), h = std::max(1, (int)view.height);
    if (viewport.texture.width == w && viewport.texture.height == h)
    {
        return;
    }
    if (viewport.id)
    {
        UnloadRenderTexture(viewport);
    }
    viewport = LoadRenderTexture(w, h);
    SetTextureFilter(viewport.texture, TEXTURE_FILTER_BILINEAR);
}

int pendingLayerSource = -1, pendingLayerTarget = -1;
using LayersPanelCallback = void (*)(float *, float *);
LayersPanelCallback layersPanelCallback = nullptr;

bool recordTreeEditable(const float *records, int root)
{
    return workspaceModelCallback(6, root, const_cast<float *>(records), nullptr, nullptr) != 0;
}

void layersPanel(const float *state, const float *records)
{
    if (!panelOpen[5])
    {
        return;
    }
    ImGui::SetNextWindowSize({320, 330}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Layers", &panelOpen[5]))
    {
        layersPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        layersPanelCallback(const_cast<float *>(state), const_cast<float *>(records));
    }
    ImGui::End();
}

void hierarchyPanel(const float *state, const float *records)
{
    if (!panelOpen[1])
    {
        return;
    }
    if (ImGui::Begin("Hierarchy", &panelOpen[1]))
    {
        hierarchyCallback(const_cast<float *>(state), const_cast<float *>(records));
    }
    ImGui::End();
}

void inspectorPanel(const float *state, const float *records)
{
    if (!panelOpen[2])
    {
        inspectorCallback(3, nullptr, nullptr, nullptr);
        transformEditing = false;
        return;
    }
    if (ImGui::Begin("Inspector", &panelOpen[2]))
    {
        transformEditing =
            inspectorCallback(0, const_cast<float *>(state), const_cast<float *>(records), nullptr) != 0;
    }
    else
    {
        inspectorCallback(3, nullptr, nullptr, nullptr);
        transformEditing = false;
    }
    ImGui::End();
}

void tilesPanel(const float *state)
{
    if (!panelOpen[4])
    {
        return;
    }
    if (ImGui::Begin("Tiles", &panelOpen[4]))
    {
        tilePaletteFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        tilePalette(state);
    }
    ImGui::End();
}

void refreshAssets()
{
    for (auto &a : assets)
    {
        if (a.handle && a.kind == 1)
        {
            ox_release_texture(a.handle);
        }
        if (a.handle && (a.kind == 2 || a.kind == 5))
        {
            ox_release_model(a.handle);
        }
        a.handle = 0;
        a.failed = false;
    }
    tileCatalogDirty = true;
}

using ResourcesCallback = void (*)(float *);
ResourcesCallback resourcesCallback = nullptr;
void resourcesPanel(const float *state)
{
    if (!panelOpen[3])
    {
        return;
    }
    if (ImGui::Begin("Resources", &panelOpen[3]))
    {
        resourcePanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        resourcesCallback(const_cast<float *>(state));
    }
    ImGui::End();
}

using WorkspaceControllerCallback = int (*)(int, int, float *, const char *);
WorkspaceControllerCallback workspaceControllerCallback = nullptr;
void focusSceneForNavigation(bool hovered)
{
    float input[]{
        hovered ? 1.f : 0.f,
        ImGui::IsMouseClicked(ImGuiMouseButton_Right) ? 1.f : 0.f,
        ImGui::IsAnyItemActive() ? 1.f : 0.f,
        ImGui::GetIO().WantTextInput ? 1.f : 0.f,
        transformEditing ? 1.f : 0.f,
        ImGui::IsDragDropActive() ? 1.f : 0.f,
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) ? 1.f : 0.f};
    if (!workspaceControllerCallback(4, 0, input, nullptr))
    {
        return;
    }
    ImGui::SetWindowFocus();
    // These panels were submitted before Scene and cached their old focus this frame.
    resourcePanelFocused = tilePaletteFocused = layersPanelFocused = tileFocused = false;
    if (sceneWindow && !glfwGetWindowAttrib(sceneWindow, GLFW_FOCUSED))
    {
        glfwFocusWindow(sceneWindow);
    }
}

using SceneToolbarCallback = void (*)(float *);
SceneToolbarCallback sceneToolbarCallback = nullptr;
using LayoutCallback = void (*)(int, int *);
LayoutCallback layoutCallback = nullptr;
void scenePanel(const float *state)
{
    sceneHovered = sceneVisible = false;
    if (!panelOpen[0])
    {
        return;
    }
    if (ImGui::Begin("Scene", &panelOpen[0],
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        sceneVisible = true;
        sceneWindow = (GLFWwindow *)ImGui::GetWindowViewport()->PlatformHandle;
        if (!sceneWindow)
        {
            sceneWindow = mainWindow;
        }
        sceneToolbarCallback(const_cast<float *>(state));
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 main = ImGui::GetMainViewport()->Pos;
        ImVec2 available = ImGui::GetContentRegionAvail();
        view = {origin.x - main.x, origin.y - main.y, std::max(1.f, available.x), std::max(1.f, available.y)};
        ensureViewport();
        ImGui::Image((ImTextureID)viewport.texture.id, {view.width, view.height}, {0, 1}, {1, 0});
        sceneHovered = ImGui::IsItemHovered();
        if (!state[6])
        {
            focusSceneForNavigation(sceneHovered);
        }
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->AddText({origin.x + 12, origin.y + 10}, IM_COL32(175, 191, 210, 255),
                      state[6] ? "PLAY / Esc to return" : "PERSPECTIVE / 2 metre grid");
        if (!state[6])
        {
            draw->AddText({origin.x + 12, origin.y + view.height - 25}, IM_COL32(110, 219, 190, 255),
                          ed_gizmo_hint());
        }
    }
    ImGui::End();
}
} // namespace

EDITOR_API void ed_workspace_init()
{
    mainWindow = glfwGetCurrentContext();
    sceneWindow = mainWindow;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoTaskBarIcon = false;
    io.ConfigViewportsNoAutoMerge = true;
    io.ConfigViewportsNoDecoration = false;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    iniPath = utf8(fs::path(GetApplicationDirectory()) / "oxygen-editor-layout.ini");
    io.IniFilename = smoke() ? nullptr : iniPath.c_str();
    int visibility[panelCount];
    for (int i = 0; i < panelCount; i++)
    {
        visibility[i] = panelOpen[i] ? 1 : 0;
    }
    layoutCallback(0, visibility);
    for (int i = 0; i < panelCount; i++)
    {
        panelOpen[i] = visibility[i] != 0;
    }
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 0;
    style.FrameRounding = 3;
    style.GrabRounding = 3;
    style.WindowPadding = {12, 12};
    style.ItemSpacing = {8, 7};
    style.FramePadding = {7, 5};
    style.Colors[ImGuiCol_WindowBg] = {.09f, .11f, .14f, 1};
    style.Colors[ImGuiCol_Header] = {.17f, .33f, .32f, 1};
    style.Colors[ImGuiCol_HeaderHovered] = {.23f, .43f, .40f, 1};
    style.Colors[ImGuiCol_DockingPreview] = {.29f, .75f, .63f, .7f};
#ifdef _WIN32
    io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 17, nullptr,
                                 io.Fonts->GetGlyphRangesCyrillic());
#endif
    ImGui_ImplGlfw_InitForOpenGL(mainWindow, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

EDITOR_API void ed_workspace_close()
{
    int visibility[panelCount];
    for (int i = 0; i < panelCount; i++)
    {
        visibility[i] = panelOpen[i] ? 1 : 0;
    }
    layoutCallback(1, visibility);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

EDITOR_API int ed_layer_delete_source()
{
    return pendingLayerSource;
}
EDITOR_API int ed_layer_delete_target()
{
    return pendingLayerTarget;
}

EDITOR_API Vector2 ed_pointer()
{
    ImVec2 mouse = ImGui::GetMousePos(), main = ImGui::GetMainViewport()->Pos;
    return {mouse.x - main.x, mouse.y - main.y};
}
EDITOR_API int ed_input_blocked()
{
    float input[]{ImGui::GetIO().WantTextInput ? 1.f : 0.f,
                  transformEditing ? 1.f : 0.f,
                  tileFocused ? 1.f : 0.f,
                  resourcePanelFocused ? 1.f : 0.f,
                  tilePaletteFocused ? 1.f : 0.f,
                  layersPanelFocused ? 1.f : 0.f};
    return workspaceControllerCallback(5, 0, input, nullptr);
}
EDITOR_API int ed_mouse(int query)
{
    if (query == 0)
    {
        return sceneVisible && sceneHovered;
    }
    if (query == 1)
    {
        return sceneHovered && ImGui::IsMouseClicked(0);
    }
    if (query == 2)
    {
        return ImGui::IsMouseDown(0);
    }
    if (query == 3)
    {
        return ImGui::IsMouseReleased(0);
    }
    if (query == 4)
    {
        return ImGui::IsMouseDown(1);
    }
    return 0;
}
EDITOR_API float ed_wheel()
{
    float input[]{
        sceneVisible ? 1.f : 0.f,
        sceneHovered ? 1.f : 0.f,
        capturedWindow ? 1.f : 0.f,
        ImGui::GetIO().WantTextInput ? 1.f : 0.f,
        transformEditing ? 1.f : 0.f,
        ImGui::IsAnyItemActive() ? 1.f : 0.f,
        ImGui::IsDragDropActive() ? 1.f : 0.f,
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) ? 1.f : 0.f};
    return workspaceControllerCallback(6, 0, input, nullptr) ? ImGui::GetIO().MouseWheel : 0;
}
EDITOR_API int ed_pressed(int key)
{
    ImGuiKey mapped = keyFor(key);
    return mapped != ImGuiKey_None && ImGui::IsKeyPressed(mapped, false);
}
EDITOR_API int ed_key_down(int key)
{
    if (capturedWindow)
    {
        return glfwGetKey(capturedWindow, key) == GLFW_PRESS;
    }
    ImGuiKey mapped = keyFor(key);
    return mapped != ImGuiKey_None && ImGui::IsKeyDown(mapped);
}
EDITOR_API int ed_control()
{
    return ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
}
EDITOR_API void ed_capture(int enabled)
{
    if (capturedWindow)
    {
        glfwSetInputMode(capturedWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        capturedWindow = nullptr;
    }
    if (enabled)
    {
        capturedWindow = sceneWindow ? sceneWindow : mainWindow;
        glfwSetInputMode(capturedWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        captureFirst = true;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }
    else
    {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    }
}
EDITOR_API float ed_look(int axis)
{
    return axis == 0 ? lookDelta.x : lookDelta.y;
}
EDITOR_API int ed_transform_values(float *values)
{
    return inspectorCallback(1, nullptr, nullptr, values);
}
EDITOR_API int ed_parent_source()
{
    return parentSource;
}
EDITOR_API int ed_parent_target()
{
    return parentTarget;
}
EDITOR_API void ed_euler(float x, float y, float z, float w, float *result)
{
    Vector3 e = QuaternionToEuler(QuaternionNormalize({x, y, z, w}));
    result[0] = e.x * RAD2DEG;
    result[1] = e.y * RAD2DEG;
    result[2] = e.z * RAD2DEG;
}

EDITOR_API int ed_ui(const float *state, const float *records)
{
    std::fill(std::begin(pressedKeys), std::end(pressedKeys), false);
    for (int key = GetKeyPressed(); key; key = GetKeyPressed())
    {
        if (key >= 0 && key < 512)
        {
            pressedKeys[key] = true;
        }
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    resourcePanelFocused = tilePaletteFocused = layersPanelFocused = false;
    lookDelta = {};
    if (capturedWindow)
    {
        double x, y;
        glfwGetCursorPos(capturedWindow, &x, &y);
        if (!captureFirst)
        {
            lookDelta = {(float)(x - lastMouseX), (float)(y - lastMouseY)};
        }
        captureFirst = false;
        lastMouseX = x;
        lastMouseY = y;
    }
    workspaceControllerCallback(0, ed_native_command(), const_cast<float *>(state), nullptr);
    ed_native_menu_state(state[6] != 0, state[3] >= 0, state[18] != 0, state[19] != 0);

    ImGuiViewport *main = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({main->WorkPos.x, main->WorkPos.y + main->WorkSize.y - 28});
    ImGui::SetNextWindowSize({main->WorkSize.x, 28});
    ImGui::SetNextWindowViewport(main->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 4});
    ImGui::Begin("Status###OxygenStatus", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav);
    ImGui::TextDisabled("%s", status.c_str());
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::SetNextWindowPos(main->WorkPos);
    ImGui::SetNextWindowSize({main->WorkSize.x, main->WorkSize.y - 28});
    ImGui::SetNextWindowViewport(main->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("Workspace###OxygenWorkspace", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
    ImGuiID dock = ImGui::GetID("OxygenDock");
    if (!ImGui::DockBuilderGetNode(dock) || resetDock)
    {
        defaultDock(dock);
        resetDock = false;
    }
    ImGui::DockSpace(dock, {0, 0});
    ImGui::End();
    ImGui::PopStyleVar();
    hierarchyPanel(state, records);
    inspectorPanel(state, records);
    tilesPanel(state);
    resourcesPanel(state);
    layersPanel(state, records);
    scenePanel(state);
    if (!sceneVisible)
    {
        if (capturedWindow)
        {
            ed_capture(0);
        }
        view = {0, 0, 1, 1};
        ensureViewport();
    }
    ed_gizmo_frame();
    workspaceControllerCallback(1, sceneVisible ? 1 : 0, const_cast<float *>(state), nullptr);
    if (IsFileDropped())
    {
        FilePathList dropped = LoadDroppedFiles();
        for (unsigned i = 0; i < dropped.count; i++)
        {
            workspaceControllerCallback(2, 0, const_cast<float *>(state), dropped.paths[i]);
        }
        UnloadDroppedFiles(dropped);
    }
    return workspaceControllerCallback(3, 0, nullptr, nullptr);
}

EDITOR_API void ed_present()
{
    rlDrawRenderBatchActive();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    GLFWwindow *previous = glfwGetCurrentContext();
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    glfwMakeContextCurrent(previous);
}

int sceneNavigationFocusTests()
{
    ImGuiIO &io = ImGui::GetIO();
    const bool oldResources = resourcePanelFocused, oldTiles = tilePaletteFocused,
               oldTileEditor = tileFocused;
    const bool oldLayers = layersPanelFocused;
    const bool oldHovered = sceneHovered, oldVisible = sceneVisible, oldEditing = transformEditing;
    int failures = 0;
    auto draw = [&]() {
        ImGuiViewport *main = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(main->ID);
        ImGui::SetNextWindowPos({main->Pos.x + 400, main->Pos.y + 20}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({300, 220}, ImGuiCond_Always);
        ImGui::Begin("Palette input regression", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted("Tile palette");
        ImGui::End();
        ImGui::SetNextWindowViewport(main->ID);
        ImGui::SetNextWindowPos({main->Pos.x + 20, main->Pos.y + 20}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({300, 220}, ImGuiCond_Always);
        ImGui::Begin("Scene input regression", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::Dummy({250, 150});
        sceneVisible = true;
        sceneHovered = ImGui::IsItemHovered();
        focusSceneForNavigation(sceneHovered);
        bool focused = ImGui::IsWindowFocused();
        ImGui::End();
        return focused;
    };
    auto begin = [&](bool right, float x, bool text) {
        ImVec2 origin = ImGui::GetMainViewport()->Pos;
        io.AddMousePosEvent(origin.x + x, origin.y + 80);
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, right);
        ImGui::NewFrame();
        resourcePanelFocused = tilePaletteFocused = layersPanelFocused = tileFocused = true;
        transformEditing = false;
        io.WantTextInput = text;
    };
    begin(false, 80, false);
    draw();
    ImGui::SetWindowFocus("Palette input regression");
    ImGui::EndFrame();

    begin(true, 80, false);
    if (!draw() || ed_input_blocked() || !ed_mouse(4) || ed_mouse(1))
    {
        failures++;
        puts("FAIL: right click must focus Scene and unblock navigation without a paint click.");
        printf("focus diagnostics: hovered=%d right=%d clicked=%d active=%d blocked=%d text=%d\n",
               sceneHovered, ed_mouse(4), ImGui::IsMouseClicked(1), ImGui::IsAnyItemActive(),
               ed_input_blocked(), io.WantTextInput);
    }
    ImGui::EndFrame();

    begin(false, 80, false);
    ImGui::SetWindowFocus("Palette input regression");
    if (draw() || !ed_input_blocked())
    {
        failures++;
        puts("FAIL: hovering Scene must not steal keyboard focus.");
    }
    ImGui::EndFrame();

    io.AddMouseWheelEvent(0, 1);
    begin(false, 80, false);
    ImGui::SetWindowFocus("Palette input regression");
    if (draw() || !ed_input_blocked() || ed_wheel() != 1)
    {
        failures++;
        puts("FAIL: wheel over Scene must zoom without taking keyboard focus.");
    }
    io.WantTextInput = true;
    if (ed_wheel() != 0)
    {
        failures++;
    }
    io.WantTextInput = false;
    ImGui::EndFrame();

    begin(true, 500, false);
    if (draw() || !ed_input_blocked())
    {
        failures++;
        puts("FAIL: right click outside the viewport must not focus Scene.");
    }
    ImGui::EndFrame();
    begin(false, 80, false);
    draw();
    ImGui::EndFrame();
    begin(true, 80, true);
    if (draw() || !ed_input_blocked())
    {
        failures++;
        puts("FAIL: active text input must keep its input protection.");
    }
    ImGui::EndFrame();
    begin(false, 80, false);
    draw();
    ImGui::EndFrame();
    resourcePanelFocused = oldResources;
    tilePaletteFocused = oldTiles;
    layersPanelFocused = oldLayers;
    tileFocused = oldTileEditor;
    sceneHovered = oldHovered;
    sceneVisible = oldVisible;
    transformEditing = oldEditing;
    if (!failures)
    {
        puts(
            "PASS: Scene navigation claims right-click focus from palette/resources without painting; hover/text protected.");
    }
    return failures;
}

int layerKeyboardTests()
{
    auto *previous = ImGui::GetCurrentContext();
    auto *context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    auto savedCommands = commands;
    auto savedLayers = worldLayers;
    auto savedFlags = layerFlags;
    layerFlags.clear();
    bool savedOpen = panelOpen[5], savedFocus = layersPanelFocused;
    replaceLayers({"Layer 1", "Layer 2"});
    panelOpen[5] = true;
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = {1200, 900};
    io.DeltaTime = 1.f / 60;
    io.IniFilename = nullptr;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    float state[32]{};
    state[18] = state[19] = 1;
    std::vector<float> records(capacity * stride, 0);
    auto frame = [&](ImGuiKey key, bool control = false, bool text = false, bool focused = true) {
        commands.clear();
        for (auto candidate : {ImGuiKey_Delete, ImGuiKey_Z, ImGuiKey_Y, ImGuiKey_Escape, ImGuiKey_F2})
        {
            io.AddKeyEvent(candidate, candidate == key);
        }
        io.AddKeyEvent(ImGuiMod_Ctrl, control);
        ImGui::NewFrame();
        if (focused)
        {
            ImGui::SetNextWindowFocus();
        }
        else
        {
            ImGui::FocusWindow(nullptr);
        }
        io.WantTextInput = text;
        layersPanel(state, records.data());
        ImGui::Render();
        return commands.empty() ? 0 : commands.front();
    };
    int failures = 0;
    frame(ImGuiKey_None);
    if (frame(ImGuiKey_Z, true) != 7)
    {
        failures++;
    }
    frame(ImGuiKey_None);
    if (frame(ImGuiKey_Y, true) != 8)
    {
        failures++;
    }
    frame(ImGuiKey_None);
    if (frame(ImGuiKey_Z, true, true) != 0)
    {
        failures++;
    }
    frame(ImGuiKey_None);
    if (frame(ImGuiKey_Z, true, false, false) != 0)
    {
        failures++;
    }
    frame(ImGuiKey_None);
    frame(ImGuiKey_Delete);
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    {
        failures++;
    }
    frame(ImGuiKey_None);
    frame(ImGuiKey_Escape);
    frame(ImGuiKey_None);
    replaceLayers({"Layer 1"});
    frame(ImGuiKey_Delete);
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    {
        failures++;
    }
    frame(ImGuiKey_None);
    frame(ImGuiKey_F2);
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    {
        failures++;
    }
    frame(ImGuiKey_None);
    frame(ImGuiKey_Escape);
    frame(ImGuiKey_None);
    frame(ImGuiKey_F2, false, true);
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    {
        failures++;
    }
    frame(ImGuiKey_None);
    ed_layer_add("Layer 2");
    ed_layer_set_flags(0, 2);
    frame(ImGuiKey_Delete);
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    {
        failures++;
    }
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previous);
    commands = savedCommands;
    replaceLayers(savedLayers, savedFlags);
    panelOpen[5] = savedOpen;
    layersPanelFocused = savedFocus;
    puts(
        failures
            ? "FAIL: Layers keyboard shortcuts."
            : "PASS: Layers F2 rename, Delete, Ctrl+Z/Y, focus/text protection, locked and last-layer guards.");
    return failures;
}

EDITOR_API int ed_workspace_tests()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGuiIO &io = ImGui::GetIO();
    ImGui::GetCurrentContext()->InputEventsQueue.clear();
    io.AddMousePosEvent(100, 100);
    io.AddMouseButtonEvent(0, true);
    io.AddMousePosEvent(200, 150);
    io.AddMouseButtonEvent(0, false);
    int failures = 0;
    ImGui::NewFrame();
    if (!ImGui::IsMouseClicked(0))
    {
        failures++;
    }
    ImGui::EndFrame();
    ImGui::NewFrame();
    if (!ImGui::IsMouseDown(0) || !ImGui::IsMouseDragging(0))
    {
        failures++;
    }
    ImGui::EndFrame();
    ImGui::NewFrame();
    if (!ImGui::IsMouseReleased(0))
    {
        failures++;
    }
    ImGui::EndFrame();
    if (failures)
    {
        puts("FAIL: fast docking/hierarchy drag input.");
    }
    else
    {
        puts("PASS: fast mouse gestures preserve a held drag frame before release.");
    }
    return failures + sceneNavigationFocusTests() + layerKeyboardTests();
}

EDITOR_API void ed_bind_workspace_model(WorkspaceModelCallback callback)
{
    workspaceModelCallback = callback;
}

EDITOR_API void ed_bind_layers_panel(LayersPanelCallback callback)
{
    layersPanelCallback = callback;
}
EDITOR_API void ed_layer_delete_view(int source, int target)
{
    pendingLayerSource = source;
    pendingLayerTarget = target;
}
#include "editor_imgui.inl"

EDITOR_API void ed_bind_inspector(InspectorCallback callback)
{
    inspectorCallback = callback;
}
EDITOR_API void ed_parent_view(int source, int target)
{
    parentSource = source;
    parentTarget = target;
}

EDITOR_API void ed_bind_hierarchy(HierarchyCallback callback)
{
    hierarchyCallback = callback;
}
EDITOR_API void ed_row_view(int selected)
{
    selectedRow = selected;
}

EDITOR_API void ed_bind_resources(ResourcesCallback callback)
{
    resourcesCallback = callback;
}
EDITOR_API void ed_assets_refresh_view()
{
    refreshAssets();
}
EDITOR_API void ed_asset_selected_view(int id)
{
    selectedAsset = id;
}
EDITOR_API void ed_asset_preview(int id)
{
    Asset *selected = asset(id);
    if (!selected)
    {
        return;
    }
    if (selected->kind == 1)
    {
        Texture2D texture = ox_lookup_texture(handle(selected));
        if (texture.id)
        {
            float scale = std::min(210.f / texture.width, 130.f / texture.height);
            ImGui::Image((ImTextureID)texture.id, {texture.width * scale, texture.height * scale});
        }
    }
    if (selected->kind == 2 && handle(selected))
    {
        renderModelPreview(id);
        if (preview.id)
        {
            ImGui::Image((ImTextureID)preview.texture.id, {216, 100}, {0, 1}, {1, 0});
        }
    }
}

EDITOR_API void ed_bind_scene_toolbar(SceneToolbarCallback callback)
{
    sceneToolbarCallback = callback;
}
EDITOR_API void ed_bind_layout(LayoutCallback callback)
{
    layoutCallback = callback;
}

EDITOR_API void ed_bind_workspace_controller(WorkspaceControllerCallback callback)
{
    workspaceControllerCallback = callback;
}
EDITOR_API void ed_panel_open(int index)
{
    if (index < 0 || index >= panelCount)
    {
        return;
    }
    panelOpen[index] = true;
    ImGui::SetWindowFocus(panelNames[index]);
}
EDITOR_API void ed_layout_reset()
{
    resetDock = true;
}
EDITOR_API void ed_window_title(const char *title)
{
    SetWindowTitle(title);
}

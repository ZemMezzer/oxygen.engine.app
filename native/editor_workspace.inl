// Docking and platform input stay here; document commands and history belong to Xenon.
namespace
{
GLFWwindow *mainWindow = nullptr, *sceneWindow = nullptr, *capturedWindow = nullptr;
bool panelOpen[5] = {true, true, true, true, true};
const char *panelNames[] = {"Scene", "Hierarchy", "Inspector", "Resources", "Tiles"};
bool sceneHovered = false, sceneVisible = false, resetDock = false, transformEditing = false;
bool numericDirty = false;
int hierarchySelection = -1, hierarchyParent = -1;
bool revealSelection = false;
bool revealPath[capacity]{};
int numericSelection = -1, parentSource = -1, parentTarget = -1;
float numeric[9]{}, pendingNumeric[9]{}, numericStart[9]{};
std::deque<int> commands;
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
    ImGuiID center = dock, left, right, bottom, tiles;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, .18f, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, .25f, &right, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, .26f, &bottom, &center);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, .43f, &tiles, &left);
    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Resources", bottom);
    ImGui::DockBuilderDockWindow("Tiles", tiles);
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

void hierarchyNode(int slot, const float *state, const float *records,
                   const std::vector<std::vector<int>> &children, int depth)
{
    if (depth >= 64)
    {
        return;
    }
    ImGui::PushID(slot);
    int type = (int)records[slot * stride];
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children[slot].empty())
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if ((int)state[3] == slot)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (revealSelection && revealPath[slot])
    {
        ImGui::SetNextItemOpen(true);
    }
    bool open = ImGui::TreeNodeEx("object", flags, "%s  #%d", names[type], slot + 1);
    if (revealSelection && (int)state[3] == slot && !ImGui::IsItemVisible())
    {
        ImGui::SetScrollHereY(.5f);
    }
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        selectedRow = slot;
        request(70);
    }
    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload("OXYGEN_OBJECT", &slot, sizeof(slot));
        ImGui::Text("%s #%d", names[type], slot + 1);
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("OXYGEN_OBJECT"))
        {
            parentSource = *(const int *)payload->Data;
            parentTarget = slot;
            request(92);
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Select"))
        {
            selectedRow = slot;
            request(70);
        }
        if (ImGui::MenuItem("Detach from parent", nullptr, false, records[slot * stride + 14] != 0))
        {
            parentSource = slot;
            parentTarget = -1;
            request(92);
        }
        ImGui::EndPopup();
    }
    if (open)
    {
        for (int child : children[slot])
        {
            hierarchyNode(child, state, records, children, depth + 1);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void hierarchyPanel(const float *state, const float *records)
{
    if (!panelOpen[1])
    {
        return;
    }
    if (ImGui::Begin("Hierarchy", &panelOpen[1]))
    {
        ImGui::BeginDisabled(state[6] != 0);
        ImGui::TextDisabled("%d objects", (int)state[4]);
        ImGui::Selectable("Scene root (drop to detach)", false);
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("OXYGEN_OBJECT"))
            {
                parentSource = *(const int *)payload->Data;
                parentTarget = -1;
                request(92);
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::Separator();
        int currentSelection = (int)state[3];
        int currentParent = currentSelection >= 0 ? (int)records[currentSelection * stride + 14] : -1;
        revealSelection = hierarchySelection != currentSelection || hierarchyParent != currentParent;
        hierarchySelection = currentSelection;
        hierarchyParent = currentParent;
        std::fill(std::begin(revealPath), std::end(revealPath), false);
        int ancestor = hierarchySelection;
        for (int depth = 0; depth < 64 && ancestor >= 0; depth++)
        {
            ancestor = (int)records[ancestor * stride + 14] - 1;
            if (ancestor >= 0)
            {
                revealPath[ancestor] = true;
            }
        }
        std::vector<std::vector<int>> children(capacity);
        std::vector<int> roots;
        for (int i = 0; i < capacity; i++)
        {
            if (!records[i * stride])
            {
                continue;
            }
            int parent = (int)records[i * stride + 14] - 1;
            if (parent >= 0 && parent < capacity)
            {
                children[parent].push_back(i);
            }
            else
            {
                roots.push_back(i);
            }
        }
        for (int slot : roots)
        {
            hierarchyNode(slot, state, records, children, 0);
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
}

void inspectorPanel(const float *state, const float *records)
{
    bool wasEditing = transformEditing;
    transformEditing = false;
    if (!panelOpen[2])
    {
        return;
    }
    if (ImGui::Begin("Inspector", &panelOpen[2]))
    {
        int selected = (int)state[3];
        if (selected >= 0)
        {
            const float *r = records + selected * stride;
            ImGui::Text("%s  #%d", names[(int)r[0]], selected + 1);
            ImGui::SeparatorText("Transform / world");
            if (selected != numericSelection || !wasEditing)
            {
                numericSelection = selected;
                for (int a = 0; a < 3; a++)
                {
                    numeric[a] = r[1 + a];
                    numeric[6 + a] = r[5 + a];
                }
                numeric[3] = r[12];
                numeric[4] = r[4];
                numeric[5] = r[13];
                numericDirty = false;
            }
            ImGui::BeginDisabled(state[6] != 0);
            const char *groups[] = {"Position", "Rotation (degrees)", "Size (metres)"};
            const char *axes[] = {"X", "Y", "Z"};
            bool commit = false;
            for (int g = 0; g < 3; g++)
            {
                ImGui::TextUnformatted(groups[g]);
                if (ImGui::BeginTable(groups[g], 3, ImGuiTableFlags_SizingStretchSame))
                {
                    for (int a = 0; a < 3; a++)
                    {
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", axes[a]);
                    }
                    for (int a = 0; a < 3; a++)
                    {
                        ImGui::TableNextColumn();
                        ImGui::PushID(g * 3 + a);
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputFloat("##value", &numeric[g * 3 + a], 0, 0, "%.3f");
                        if (ImGui::IsItemActivated())
                        {
                            std::copy(numeric, numeric + 9, numericStart);
                        }
                        if (ImGui::IsItemEdited())
                        {
                            numericDirty = true;
                        }
                        bool active = ImGui::IsItemActive();
                        transformEditing |= active;
                        if (active && ImGui::IsKeyPressed(ImGuiKey_Escape))
                        {
                            std::copy(numericStart, numericStart + 9, numeric);
                            numericDirty = false;
                        }
                        if (numericDirty && (ImGui::IsItemDeactivatedAfterEdit() ||
                                             (active && ImGui::IsKeyPressed(ImGuiKey_Enter))))
                        {
                            commit = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
            if (commit)
            {
                std::copy(numeric, numeric + 9, pendingNumeric);
                commands.push_front(90);
                numericDirty = false;
            }
            ImGui::TextDisabled("Enter / leave field: apply. Esc: cancel.");
            ImGui::SeparatorText("Hierarchy");
            int parent = (int)r[14] - 1;
            if (parent < 0)
            {
                ImGui::TextDisabled("Parent: Scene root");
            }
            else
            {
                ImGui::Text("Parent: %s #%d", names[(int)records[parent * stride]], parent + 1);
                if (ImGui::Button("Detach from parent"))
                {
                    parentSource = selected;
                    parentTarget = -1;
                    request(92);
                }
            }
            ImGui::SeparatorText("Object");
            const char *modes[] = {"Static collider", "Dynamic body", "No collision"};
            if (ImGui::Button(modes[(int)r[10]], {-1, 0}))
            {
                request(60);
            }
            ImGui::BeginDisabled(ed_asset_kind(selectedAsset) != 1);
            if (ImGui::Button("Apply selected texture", {-1, 0}))
            {
                request(63);
            }
            ImGui::EndDisabled();
            if (r[0] == 7 || r[0] == 8)
            {
                ImGui::Text("Strength / volume: %.2f", r[11]);
                if (ImGui::Button("-"))
                {
                    request(61);
                }
                ImGui::SameLine();
                if (ImGui::Button("+"))
                {
                    request(62);
                }
            }
            if (ImGui::Button("Duplicate  Ctrl+D", {-1, 0}))
            {
                request(91);
            }
            if (ImGui::Button("Focus  F"))
            {
                request(64);
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete"))
            {
                request(50);
            }
            ImGui::EndDisabled();
        }
        else
        {
            ImGui::TextWrapped("Select an object in Scene or Hierarchy to edit its properties.");
        }
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
        ImGui::BeginDisabled(state[6] != 0);
        ImGui::TextDisabled("Built-in tile set / 2 metre grid");
        for (int i = 1; i <= 10; i++)
        {
            bool active = (int)state[0] == i;
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.16f, .42f, .38f, 1));
            }
            if (ImGui::Button(i == 10 ? "Erase" : names[i], {-1, 28}))
            {
                request(20 + i);
            }
            if (active)
            {
                ImGui::PopStyleColor();
            }
        }
        ImGui::Separator();
        ImGui::Text("Layer %d  /  Y = %.0f m", (int)state[1], state[1] * 2);
        if (ImGui::Button("Layer -"))
        {
            request(40);
        }
        ImGui::SameLine();
        if (ImGui::Button("Layer +"))
        {
            request(41);
        }
        if (ImGui::Button(TextFormat("Rotate brush: %.0f", state[2]), {-1, 0}))
        {
            request(42);
        }
        if (ImGui::Button("Select objects  [V]", {-1, 0}))
        {
            request(20);
        }
        ImGui::EndDisabled();
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
        if (a.handle && a.kind == 2)
        {
            ox_release_model(a.handle);
        }
        a.handle = 0;
        a.failed = false;
    }
    scan();
    message("Resources refreshed.");
}

void resourcesPanel(const float *state)
{
    if (!panelOpen[3])
    {
        return;
    }
    if (ImGui::Begin("Resources", &panelOpen[3]))
    {
        ImGui::BeginDisabled(state[6] != 0);
        if (ImGui::Button("Import file"))
        {
            request(9);
        }
        ImGui::SameLine();
        if (ImGui::Button("Import folder"))
        {
            request(10);
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
        {
            refreshAssets();
        }
        ImGui::SameLine();
        if (ImGui::Button("Up") && !folder.empty())
        {
            folder = folder.parent_path();
        }
        ImGui::TextDisabled("Assets / %s", utf8(folder).c_str());
        ImGui::Separator();
        ImGui::BeginChild("Files", {std::max(120.f, ImGui::GetContentRegionAvail().x - 235), 0},
                          ImGuiChildFlags_Borders);
        std::vector<fs::path> folders;
        for (auto &a : assets)
        {
            fs::path rel = a.relative.lexically_relative(folder.empty() ? fs::path(".") : folder);
            if (rel.empty() || *rel.begin() == ".." || !rel.has_parent_path())
            {
                continue;
            }
            fs::path child = folder / *rel.begin();
            if (std::find(folders.begin(), folders.end(), child) == folders.end())
            {
                folders.push_back(child);
            }
        }
        int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / 130));
        if (ImGui::BeginTable("Cards", columns))
        {
            for (const auto &f : folders)
            {
                ImGui::TableNextColumn();
                if (ImGui::Button(("[Folder]\n" + utf8(f.filename())).c_str(), {-1, 68}))
                {
                    folder = f;
                    break;
                }
            }
            for (auto &a : assets)
            {
                if (a.relative.parent_path() != folder)
                {
                    continue;
                }
                ImGui::TableNextColumn();
                ImGui::PushID(a.id);
                const char *types[] = {"", "Texture", "Model", "Audio", "File"};
                bool selected = a.id == selectedAsset;
                if (selected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.16f, .42f, .38f, 1));
                }
                if (ImGui::Button(types[a.kind], {-1, 43}))
                {
                    selectedAsset = a.id;
                    request(71);
                }
                if (selected)
                {
                    ImGui::PopStyleColor();
                }
                ImGui::TextWrapped("%s", utf8(a.relative.filename()).c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Preview", {0, 0});
        Asset *selected = asset(selectedAsset);
        if (selected)
        {
            ImGui::TextWrapped("%s", utf8(selected->relative).c_str());
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
                // Render the model in the shared context; all detached panels can display this texture.
                renderModelPreview(selectedAsset);
                if (preview.id)
                {
                    ImGui::Image((ImTextureID)preview.texture.id, {216, 100}, {0, 1}, {1, 0});
                }
            }
            if (selected->kind == 3 && ImGui::Button("Preview audio"))
            {
                request(12);
            }
        }
        else
        {
            ImGui::TextWrapped("Import resources or drop files into the main editor window.");
        }
        ImGui::EndChild();
        ImGui::EndDisabled();
    }
    ImGui::End();
}

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
        ImGui::BeginDisabled(state[6] != 0);
        const char *tools[] = {"Move [W]", "Rotate [E]", "Scale [R]"};
        for (int i = 0; i < 3; i++)
        {
            if (i)
            {
                ImGui::SameLine();
            }
            bool active = state[0] == 0 && state[20] == i + 1;
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.16f, .42f, .38f, 1));
            }
            if (ImGui::Button(tools[i]))
            {
                request(80 + i);
            }
            if (active)
            {
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(state[6] ? "Stop [Esc]" : "Play [F5]"))
        {
            request(6);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%d FPS", GetFPS());
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 main = ImGui::GetMainViewport()->Pos;
        ImVec2 available = ImGui::GetContentRegionAvail();
        view = {origin.x - main.x, origin.y - main.y, std::max(1.f, available.x), std::max(1.f, available.y)};
        ensureViewport();
        ImGui::Image((ImTextureID)viewport.texture.id, {view.width, view.height}, {0, 1}, {1, 0});
        sceneHovered = ImGui::IsItemHovered();
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
    if (!smoke())
    {
        std::ifstream f(iniPath + ".panels");
        std::string magic;
        int version = 0;
        f >> magic >> version;
        bool values[5]{};
        bool valid = magic == "OXYGEN_PANELS" && version == 1;
        for (int i = 0; i < 5; i++)
        {
            int value = -1;
            f >> value;
            valid &= value == 0 || value == 1;
            values[i] = value == 1;
        }
        if (f && valid)
        {
            std::copy(values, values + 5, panelOpen);
        }
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
    if (!smoke())
    {
        std::ofstream f(iniPath + ".panels");
        f << "OXYGEN_PANELS 1\n";
        for (bool open : panelOpen)
        {
            f << (open ? 1 : 0) << ' ';
        }
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

EDITOR_API Vector2 ed_pointer()
{
    ImVec2 mouse = ImGui::GetMousePos(), main = ImGui::GetMainViewport()->Pos;
    return {mouse.x - main.x, mouse.y - main.y};
}
EDITOR_API int ed_input_blocked()
{
    return ImGui::GetIO().WantTextInput || transformEditing;
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
    return sceneHovered ? ImGui::GetIO().MouseWheel : 0;
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
    for (int i = 0; i < 9; i++)
    {
        float v = pendingNumeric[i];
        if (!std::isfinite(v) || (i < 3 && std::fabs(v) > 10000) ||
            (i >= 3 && i < 6 && std::fabs(v) > 36000) || (i >= 6 && (v < .02f || v > 1000)))
        {
            message("Invalid transform: position +/-10000, rotation +/-36000, size 0.02..1000.");
            numericSelection = -1;
            return 0;
        }
    }
    std::copy(pendingNumeric, pendingNumeric + 9, values);
    return 1;
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
    int native = ed_native_command();
    if (native >= 200 && native < 205)
    {
        panelOpen[native - 200] = true;
        ImGui::SetWindowFocus(panelNames[native - 200]);
    }
    else if (native == 210)
    {
        resetDock = true;
    }
    else if (native)
    {
        if (numericDirty)
        {
            std::copy(numeric, numeric + 9, pendingNumeric);
            request(90);
            numericDirty = false;
        }
        request(native);
    }
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
    scenePanel(state);
    if (!sceneVisible)
    {
        if (capturedWindow)
        {
            ed_capture(0);
        }
        if (state[6])
        {
            request(6);
        }
        view = {0, 0, 1, 1};
        ensureViewport();
    }
    ed_gizmo_frame();
    std::string title = "Oxygen - " + utf8(root.filename()) + " / " +
                        (currentScene.empty() ? "Untitled" : utf8(currentScene.filename())) +
                        (state[5] ? " *" : "");
    SetWindowTitle(title.c_str());
    if (IsFileDropped())
    {
        FilePathList dropped = LoadDroppedFiles();
        if (!state[6])
        {
            for (unsigned i = 0; i < dropped.count; i++)
            {
                importPath(fromUtf8(dropped.paths[i]));
            }
        }
        UnloadDroppedFiles(dropped);
    }
    if (commands.empty())
    {
        return 0;
    }
    int action = commands.front();
    commands.pop_front();
    return action;
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
    return failures;
}

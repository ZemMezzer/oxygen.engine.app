int tileKeyboardTests(float *data, const fs::path &existingTile)
{
    int failures = 0;
    auto *previous = ImGui::GetCurrentContext();
    auto *context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = {1200, 900};
    io.DeltaTime = 1.f / 60;
    io.IniFilename = nullptr;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    float selection[tileVertexMax]{};
    std::fill(selection, selection + 4, 1.f);
    tileOpen = true;
    tileMode = 2;
    auto frame = [&](bool remove, bool undo, bool text = false, bool focused = true) {
        io.AddKeyEvent(ImGuiKey_Delete, remove);
        io.AddKeyEvent(ImGuiMod_Ctrl, undo);
        io.AddKeyEvent(ImGuiKey_Z, undo);
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
        int action = ed_tile_ui(data, selection, 1, 2);
        ImGui::Render();
        return action;
    };
    frame(false, false);
    if (frame(true, false) != 27)
    {
        failures++;
    }
    frame(false, false);
    tileMode = 1;
    if (frame(true, false) != 7)
    {
        failures++;
    }
    frame(false, false);
    if (frame(false, true) != 20)
    {
        failures++;
    }
    frame(false, false);
    if (frame(true, false, true) != 0)
    {
        failures++;
    }
    frame(false, false);
    if (frame(true, false, false, false) != 0)
    {
        failures++;
    }
    frame(false, false);
    tileOpen = tileFocused = tileDirty = false;
    tileMode = 2;

    // Exercise the real palette handler, including persisted membership and Undo.
    fs::path destination = fs::path("Tiles") / "tile_KeyboardTest";
    fs::create_directories(root / "Assets" / destination);
    tileSetWrite(destination, {"Keyboard", {existingTile}});
    tileSet = paletteSelectionSet = destination;
    tilePaletteCallback(4, nullptr, 0);
    tilePaletteCallback(5, nullptr, registerAsset(existingTile));
    tileCatalogDirty = true;
    float state[16]{};
    auto paletteFrame = [&](bool remove, bool undo) {
        io.AddKeyEvent(ImGuiKey_Delete, remove);
        io.AddKeyEvent(ImGuiMod_Ctrl, undo);
        io.AddKeyEvent(ImGuiKey_Z, undo);
        ImGui::NewFrame();
        ImGui::SetNextWindowFocus();
        ImGui::Begin("Tiles keyboard test");
        tilePalette(state);
        ImGui::End();
        ImGui::Render();
    };
    paletteFrame(false, false);
    paletteFrame(true, false);
    TileSetInfo loaded;
    if (!tileSetRead(destination, loaded) || !loaded.members.empty() ||
        !fs::is_regular_file(root / "Assets" / existingTile))
    {
        failures++;
    }
    paletteFrame(false, false);
    paletteFrame(false, true);
    if (!tileSetRead(destination, loaded) || loaded.members != std::vector<fs::path>{existingTile} ||
        tileSetName(destination) != "Keyboard")
    {
        failures++;
    }
    paletteFrame(false, false);
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previous);
    puts(failures ? "FAIL: tile keyboard routing."
                  : "PASS: Delete/Undo in Face, vertex and tileset UI; text input and focus protection.");
    return failures;
}

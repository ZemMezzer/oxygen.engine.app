int tileHandleTests(const float *data)
{
    int failures = 0;
    auto *previous = ImGui::GetCurrentContext();
    auto *context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = {640, 480};
    io.DeltaTime = 1.f / 60;
    io.IniFilename = nullptr;
    // Each invocation is an explicit input frame; do not defer its release/key events.
    io.ConfigInputTrickleEventQueue = false;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    float selection[tileVertexMax]{};
    for (int i = 0; i < 4; i++)
    {
        selection[i] = 1;
    }
    Camera3D camera{{0, 1, 6}, {0, 1, 0}, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
    bool previousFocus = tileFocused;
    tileFocused = true;
    std::array<float, tileDataSize> displayedData{};
    std::copy(data, data + tileDataSize, displayedData.begin());
    Vector2 drawnCenter{};
    auto frame = [&](float x, float y, bool down, bool escape = false) {
        io.AddMousePosEvent(x, y);
        io.AddMouseButtonEvent(0, down);
        io.AddKeyEvent(ImGuiKey_Escape, escape);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({640, 480});
        ImGui::Begin("Tile gizmo test");
        int action = 0;
        tileGizmo(displayedData.data(), selection, camera, {0, 0}, 640, 480, true, false, false, .25f,
                  action);
        // The final four vertices are the visible central square, not the drag's math pivot.
        if (tileTool != 1)
        {
            const auto &vertices = ImGui::GetWindowDrawList()->VtxBuffer;
            drawnCenter = {};
            for (int i = vertices.Size - 4; i < vertices.Size; i++)
            {
                drawnCenter.x += vertices[i].pos.x * .25f;
                drawnCenter.y += vertices[i].pos.y * .25f;
            }
        }
        ImGui::End();
        ImGui::Render();
        return action;
    };
    tileTool = 0;
    resetTileHandle();
    frame(375, 240, false);
    frame(375, 240, true);
    frame(415, 240, true);
    float expected = 40 * 12 * tanf(25 * DEG2RAD) / 480;
    if (!tileHandle.active || fabsf(tileValues[0] - expected) > .001f || tileValues[1] != 0 ||
        tileValues[2] != 0)
    {
        puts("FAIL: tile move axis.");
        failures++;
    }
    if (frame(425, 240, false) != 25 || tileValues[15] != 1 || tileHandle.active || tileValues[0] <= expected)
    {
        puts("FAIL: tile release final position.");
        failures++;
    }
    frame(375, 240, true);
    if (frame(400, 240, true, true) != 17 || tileHandle.active)
    {
        puts("FAIL: tile cancel.");
        failures++;
    }
    frame(400, 240, false, false);
    tileTool = 2;
    frame(375, 240, true);
    frame(415, 240, true);
    if (fabsf(tileValues[6] - 1.5f) > .001f || tileValues[7] != 1 || tileValues[8] != 1)
    {
        printf("FAIL: tile scale %.4f %.4f %.4f.\n", tileValues[6], tileValues[7], tileValues[8]);
        failures++;
    }
    frame(415, 240, false);
    tileTool = 1;
    frame(376, 184, true);
    frame(264, 184, true);
    if (tileHandle.handle != 2 || fabsf(tileValues[5] - 90) > .1f)
    {
        printf("FAIL: tile rotation handle=%d angle=%.3f.\n", tileHandle.handle, tileValues[5]);
        failures++;
    }
    frame(264, 184, false);
    tileTool = 0;
    frame(375, 240, true);
    frame(415, 240, true);
    // Simulate Xenon applying the previous frame's command before drawing the next frame.
    for (int i = 0; i < 4; i++)
    {
        displayedData[i * 4 + 1] = data[i * 4 + 1] + tileValues[0];
    }
    frame(435, 240, true);
    if (Vector2Distance(drawnCenter, {360, 240}) > .01f ||
        Vector3Distance(tileHandle.pivot, {0, 1, 0}) > .001f ||
        fabsf(tileValues[0] - expected * 1.5f) > .001f)
    {
        puts("FAIL: visible gizmo must follow moved geometry while its drag origin stays fixed.");
        failures++;
    }
    for (int i = 0; i < 4; i++)
    {
        displayedData[i * 4 + 1] = data[i * 4 + 1] + tileValues[0];
    }
    frame(435, 240, false);
    if (Vector2Distance(drawnCenter, {380, 240}) > .01f || tileHandle.active)
    {
        puts("FAIL: gizmo follows geometry on release.");
        failures++;
    }
    resetTileHandle();
    tileTool = 0;
    tileFocused = previousFocus;
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previous);
    std::array<float, tileDataSize> preview{};
    float uv[8]{.25f, .75f, .75f, .75f, .75f, .25f, .25f, .25f};
    for (int plane = 0; plane < 3; plane++)
    {
        tilePreviewData(preview.data(), {2, 3, 4}, {2, 4}, plane, uv);
        Vector3 a = tilePosition(preview.data(), 0), b = tilePosition(preview.data(), 2);
        Vector3 center = Vector3Scale(Vector3Add(a, b), .5f);
        if (Vector3Distance(center, {2, 3, 4}) > .001f || fabsf(Vector3Distance(a, b) - sqrtf(20)) > .001f ||
            preview[tileFaceOffset + 5] != .25f || preview[tileFaceOffset + 12] != .25f)
        {
            failures++;
        }
    }
    puts(failures
             ? "FAIL: tile gizmo pointer/preview regression."
             : "PASS: tile gizmo follows geometry; stable drag/release/cancel, rotation, scale and preview.");
    return failures;
}

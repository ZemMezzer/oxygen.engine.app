// Read-only state used to draw the rectangle. The gesture and snapshot are owned by Xenon.
struct TileBoxSelection
{
    bool active = false;
    bool dragged = false;
    Vector2 start{};
} tileBox;

void tileSelectionOrder(const float *selection)
{
    tileOrder.clear();
    for (int i = 0; i < tileVertexMax; i++)
    {
        if (selection[i])
        {
            tileOrder.push_back(i);
        }
    }
}

void tileBoxBegin(Vector2 start, const float *selection, bool additive, bool faces = false,
                  int previousFace = -1)
{
    float input[]{start.x, start.y, additive ? 1.f : 0.f, faces ? 1.f : 0.f, (float)previousFace};
    tileSelectionCallback(0, nullptr, const_cast<float *>(selection), nullptr, input);
    tileBox = {true, false, start};
}

// Selection is recomputed from the press snapshot: shrinking or reversing the
// rectangle never leaves behind vertices touched by an earlier, larger rectangle.
void tileBoxUpdate(const float *data, float *selection, Camera3D camera, int width, int height, Vector2 end,
                   bool dragged, bool released, bool cancel)
{
    float cameraData[]{camera.position.x, camera.position.y, camera.position.z, camera.target.x,
                       camera.target.y,   camera.target.z,   camera.up.x,       camera.up.y,
                       camera.up.z,       camera.fovy};
    float input[]{end.x,
                  end.y,
                  (float)width,
                  (float)height,
                  dragged ? 1.f : 0.f,
                  released ? 1.f : 0.f,
                  cancel ? 1.f : 0.f,
                  (float)tileFace};
    tileFace = tileSelectionCallback(1, const_cast<float *>(data), selection, cameraData, input);
    tileBox.active = tileSelectionCallback(2, nullptr, nullptr, nullptr, nullptr) != 0;
    tileBox.dragged = tileBox.dragged || dragged;
    tileSelectionOrder(selection);
}

void tileBoxDraw(ImVec2 origin, int width, int height, Vector2 end)
{
    end.x = std::clamp(end.x, 0.f, (float)width);
    end.y = std::clamp(end.y, 0.f, (float)height);
    ImVec2 low{origin.x + std::min(tileBox.start.x, end.x), origin.y + std::min(tileBox.start.y, end.y)};
    ImVec2 high{origin.x + std::max(tileBox.start.x, end.x), origin.y + std::max(tileBox.start.y, end.y)};
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + width, origin.y + height}, true);
    draw->AddRectFilled(low, high, IM_COL32(90, 165, 255, 35));
    draw->AddRect(low, high, IM_COL32(120, 195, 255, 255), 0, 0, 1.5f);
    draw->PopClipRect();
}

int tileFaceBoxTests()
{
    int failures = 0;
    int previousFace = tileFace;
    auto previousOrder = tileOrder;
    Camera3D camera{{0, 1, 6}, {0, 1, 0}, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
    std::array<float, tileDataSize> data{};
    float selection[tileVertexMax]{};
    for (int face = 0; face < 3; face++)
    {
        int offset = tileFaceOffset + face * tileFaceStride;
        data[offset] = 1;
        for (int corner = 0; corner < 4; corner++)
        {
            int vertex = face * 4 + corner;
            data[offset + corner + 1] = (float)vertex;
            data[vertex * 4] = 1;
            data[vertex * 4 + 1] = face * 2.f - 2 + (corner == 1 || corner == 2 ? .5f : -.5f);
            data[vertex * 4 + 2] = corner >= 2 ? 2.f : 0.f;
        }
    }
    auto selected = [&](int face) {
        return selection[face * 4] && selection[face * 4 + 1] && selection[face * 4 + 2] &&
               selection[face * 4 + 3];
    };
    auto update = [&](Vector2 end, bool release = false, bool cancel = false) {
        tileBoxUpdate(data.data(), selection, camera, 640, 480, end, true, release, cancel);
    };
    // Reverse rectangle selects two complete faces. Shrinking removes the second.
    tileBoxBegin({380, 340}, selection, false, true);
    update({90, 140});
    if (!selected(0) || !selected(1) || selected(2) || tileOrder.size() != 8 || tileFace != 0)
    {
        failures++;
    }
    update({210, 140}, true);
    if (selected(0) || !selected(1) || selected(2) || tileOrder.size() != 4)
    {
        failures++;
    }
    // Ctrl adds the left face; cancellation restores both the mask and active face.
    tileBoxBegin({90, 140}, selection, true, true, 1);
    update({210, 340});
    if (!selected(0) || !selected(1))
    {
        failures++;
    }
    update({}, false, true);
    if (selected(0) || !selected(1) || tileFace != 1 || tileBox.active)
    {
        failures++;
    }
    tileBoxBegin({90, 140}, selection, true, true, 1);
    update({210, 340}, true);
    if (!selected(0) || !selected(1) || tileOrder.size() != 8)
    {
        failures++;
    }
    // A rectangle touching only some corners must not leave partially selected faces.
    tileBoxBegin({90, 140}, selection, false, true);
    update({160, 340}, true);
    if (!tileOrder.empty() || tileFace != -1)
    {
        failures++;
    }
    // Geometry behind/crossing the camera plane is excluded. Outside release is clamped.
    data[3] = 7;
    tileBoxBegin({0, 0}, selection, false, true);
    update({900, 900}, true);
    if (selected(0) || !selected(1) || !selected(2) || tileBox.active)
    {
        failures++;
    }
    // An empty click clears; Ctrl+empty click keeps the prior group.
    tileBoxBegin({10, 10}, selection, true, true);
    tileBoxUpdate(data.data(), selection, camera, 640, 480, {10, 10}, false, true, false);
    if (!selected(1) || !selected(2))
    {
        failures++;
    }
    tileBoxBegin({10, 10}, selection, false, true);
    tileBoxUpdate(data.data(), selection, camera, 640, 480, {10, 10}, false, true, false);
    if (!tileOrder.empty())
    {
        failures++;
    }
    tileBox = {};
    tileFace = previousFace;
    tileOrder = previousOrder;
    puts(
        failures
            ? "FAIL: Face rectangle selection."
            : "PASS: Face rectangle groups, replace/add, shrink, cancel, whole-face containment and camera clipping.");
    return failures;
}

int tileBoxTests(const float *data)
{
    int failures = 0;
    Camera3D camera{{0, 1, 6}, {0, 1, 0}, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
    float selection[tileVertexMax]{};
    selection[0] = 1;
    auto previousOrder = tileOrder;
    // Reverse-direction rectangle selects the two upper vertices, replacing the old selection.
    tileBoxBegin({430, 200}, selection, false);
    tileBoxUpdate(data, selection, camera, 640, 480, {210, 130}, true, false, false);
    if (selection[0] || selection[1] || !selection[2] || !selection[3])
    {
        failures++;
    }
    // Shrinking must remove a previously covered vertex.
    tileBoxUpdate(data, selection, camera, 640, 480, {390, 130}, false, false, false);
    if (selection[3] || !selection[2])
    {
        failures++;
    }
    tileBoxUpdate(data, selection, camera, 640, 480, {}, true, false, true);
    if (!selection[0] || selection[2] || selection[3] || tileBox.active)
    {
        failures++;
    }
    // Ctrl adds the two upper vertices and preserves the original lower vertex.
    tileBoxBegin({210, 130}, selection, true);
    tileBoxUpdate(data, selection, camera, 640, 480, {430, 200}, true, true, false);
    if (!selection[0] || selection[1] || !selection[2] || !selection[3] || tileBox.active)
    {
        failures++;
    }
    // A click on empty space clears the selection without dirtying geometry.
    tileBoxBegin({10, 10}, selection, false);
    tileBoxUpdate(data, selection, camera, 640, 480, {10, 10}, false, true, false);
    if (!tileOrder.empty())
    {
        failures++;
    }
    // Release outside the viewport is clamped; vertices behind the camera are excluded.
    std::array<float, tileDataSize> behind{};
    std::copy(data, data + tileDataSize, behind.begin());
    behind[3] = 7;
    tileBoxBegin({0, 0}, selection, false);
    tileBoxUpdate(behind.data(), selection, camera, 640, 480, {900, 900}, true, true, false);
    if (selection[0] || !selection[1] || !selection[2] || !selection[3])
    {
        failures++;
    }
    tileBox = {};
    tileOrder = previousOrder;
    puts(failures ? "FAIL: tile rectangle selection."
                  : "PASS: vertex rectangle replace/add, reverse/shrink, cancel and outside release.");
    return failures + tileFaceBoxTests();
}

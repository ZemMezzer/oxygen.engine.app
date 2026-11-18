// Tile-specific handles use the floating viewport's coordinates, independently
// of the scene gizmo.
enum class TileIcon
{
    Object,
    Vertex,
    Face,
    Move,
    Rotate,
    Scale,
    Undo,
    Redo,
    Frame,
    Save,
    Flip,
    Delete,
    Mirror
};
bool tileIcon(const char *id, TileIcon icon, bool selected = false)
{
    ImGui::PushID(id);
    if (selected)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(178, 133, 35, 255));
    }
    bool clicked = ImGui::Button("##tool", {32, 32});
    if (selected)
    {
        ImGui::PopStyleColor();
    }
    ImVec2 p = ImGui::GetItemRectMin();
    ImDrawList *d = ImGui::GetWindowDrawList();
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    auto point = [&](float x, float y) { return ImVec2{p.x + x, p.y + y}; };
    auto line = [&](float x, float y, float a, float b) {
        d->AddLine(point(x, y), point(a, b), color, 1.8f);
    };
    auto arrow = [&](float x, float y, float a, float b) {
        line(x, y, a, b);
        Vector2 v = Vector2Scale(Vector2Normalize({a - x, b - y}), 4);
        d->AddTriangleFilled(point(a, b), point(a - v.x - v.y * .65f, b - v.y + v.x * .65f),
                             point(a - v.x + v.y * .65f, b - v.y - v.x * .65f), color);
    };
    if (icon == TileIcon::Vertex || icon == TileIcon::Face || icon == TileIcon::Object)
    {
        d->AddRect(point(7, 10), point(22, 25), color, 0, 0, 1.6f);
        if (icon == TileIcon::Vertex)
        {
            for (int i = 0; i < 4; i++)
            {
                d->AddRectFilled(point((i & 1) ? 19 : 4, (i & 2) ? 22 : 7),
                                 point((i & 1) ? 25 : 10, (i & 2) ? 28 : 13), color);
            }
        }
        if (icon == TileIcon::Face)
        {
            d->AddRectFilled(point(10, 13), point(19, 22), color);
        }
        if (icon == TileIcon::Object)
        {
            line(7, 10, 12, 5);
            line(22, 10, 27, 5);
            line(22, 25, 27, 20);
            line(12, 5, 27, 5);
            line(27, 5, 27, 20);
        }
    }
    else if (icon == TileIcon::Move)
    {
        arrow(16, 16, 16, 5);
        arrow(16, 16, 27, 16);
        arrow(16, 16, 5, 16);
        arrow(16, 16, 16, 27);
    }
    else if (icon == TileIcon::Scale)
    {
        line(8, 24, 24, 8);
        d->AddRect(point(5, 21), point(11, 27), color, 0, 0, 2);
        d->AddRectFilled(point(21, 5), point(27, 11), color);
    }
    else if (icon == TileIcon::Rotate || icon == TileIcon::Undo || icon == TileIcon::Redo)
    {
        bool reverse = icon == TileIcon::Redo;
        for (int i = 0; i < 20; i++)
        {
            float a = -.7f + i * .22f, b = a + .22f;
            float sign = reverse ? -1.f : 1.f;
            line(16 + sign * cosf(a) * 10, 16 + sinf(a) * 10, 16 + sign * cosf(b) * 10, 16 + sinf(b) * 10);
        }
        if (reverse)
        {
            arrow(23, 12, 23, 5);
        }
        else
        {
            arrow(9, 12, 9, 5);
        }
    }
    else if (icon == TileIcon::Save)
    {
        d->AddRect(point(6, 5), point(26, 27), color, 0, 0, 2);
        d->AddRect(point(11, 5), point(21, 13), color);
        d->AddRect(point(10, 19), point(23, 27), color);
    }
    else if (icon == TileIcon::Frame)
    {
        for (int i = 0; i < 4; i++)
        {
            float x = (i & 1) ? 26 : 6, y = (i & 2) ? 26 : 6;
            line(x, y, x + ((i & 1) ? -6 : 6), y);
            line(x, y, x, y + ((i & 2) ? -6 : 6));
        }
        d->AddCircle(point(16, 16), 3, color);
    }
    else if (icon == TileIcon::Delete)
    {
        line(8, 10, 24, 10);
        line(12, 6, 20, 6);
        line(10, 10, 12, 26);
        line(12, 26, 22, 26);
        line(22, 26, 24, 10);
    }
    else
    {
        line(16, 5, 16, 27);
        arrow(13, 16, 5, 16);
        arrow(19, 16, 27, 16);
    }
    ImGui::PopID();
    return clicked;
}

struct TileHandleView
{
    bool active = false;
    int handle = -1;
    Vector3 pivot{};
} tileHandle;
using TileGizmoCallback = int (*)(int, float *, float *, float *, float *, float *, float *);
TileGizmoCallback tileGizmoCallback = nullptr;
void resetTileHandle()
{
    tileHandle = {};
    tileGizmoCallback(1, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
}
int tileTool = 0; // Move, rotate, scale.
Vector3 tileAxis(int i)
{
    return i == 0 ? Vector3{1, 0, 0} : i == 1 ? Vector3{0, 1, 0} : Vector3{0, 0, 1};
}
Vector3 tileRing(Vector3 pivot, int axis, float a, float radius)
{
    return Vector3Add(pivot, Vector3Add(Vector3Scale(tileAxis((axis + 1) % 3), cosf(a) * radius),
                                        Vector3Scale(tileAxis((axis + 2) % 3), sinf(a) * radius)));
}
// Returns true while the handle owns the pointer, including release/cancel
// frames.
bool tileGizmo(const float *data, const float *selection, Camera3D camera, ImVec2 origin, int width,
               int height, bool hovered, bool blocked, bool snap, float grid, int &action)
{
    ImGuiIO &io = ImGui::GetIO();
    float context[]{camera.position.x,
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
    float input[]{
        io.MousePos.x - origin.x,
        io.MousePos.y - origin.y,
        (float)tileTool,
        hovered ? 1.f : 0.f,
        blocked ? 1.f : 0.f,
        snap ? 1.f : 0.f,
        grid,
        ImGui::IsMouseClicked(0) ? 1.f : 0.f,
        (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(1) || !tileFocused || io.AppFocusLost)
            ? 1.f
            : 0.f,
        ImGui::IsMouseDown(0) ? 1.f : 0.f};
    float display[12]{};
    int result = tileGizmoCallback(0, const_cast<float *>(data), const_cast<float *>(selection), context,
                                   input, tileValues, display);
    tileHandle.active = display[1] != 0;
    tileHandle.handle = (int)display[2];
    tileHandle.pivot = {display[3], display[4], display[5]};
    if (result)
    {
        action = result;
    }
    if (!display[0])
    {
        return false;
    }
    Vector3 drawPivot{display[6], display[7], display[8]};
    float drawRadius = display[9];
    int hit = (int)display[10];
    bool consumed = display[11] != 0;
    auto project = [&](Vector3 p) { return GetWorldToScreenEx(p, camera, width, height); };
    auto screen = [&](Vector2 p) { return ImVec2{origin.x + p.x, origin.y + p.y}; };
    Vector2 center = project(drawPivot);
    ImDrawList *d = ImGui::GetWindowDrawList();
    d->PushClipRect(origin, {origin.x + width, origin.y + height}, true);
    ImU32 colors[3]{IM_COL32(240, 85, 85, 255), IM_COL32(100, 225, 130, 255), IM_COL32(90, 165, 255, 255)};
    for (int a = 0; a < 3; a++)
    {
        ImU32 color =
            (tileHandle.active ? tileHandle.handle == a : hit == a) ? IM_COL32(255, 210, 70, 255) : colors[a];
        if (tileTool == 1)
        {
            for (int s = 0; s < 64; s++)
            {
                d->AddLine(screen(project(tileRing(drawPivot, a, s * 2 * PI / 64, drawRadius))),
                           screen(project(tileRing(drawPivot, a, (s + 1) * 2 * PI / 64, drawRadius))), color,
                           2.5f);
            }
        }
        else
        {
            Vector2 end = project(Vector3Add(drawPivot, Vector3Scale(tileAxis(a), drawRadius)));
            if (Vector2Distance(center, end) < 12)
            {
                continue;
            }
            d->AddLine(screen(center), screen(end), color, 3);
            Vector2 v = Vector2Scale(Vector2Normalize(Vector2Subtract(end, center)), 10);
            if (tileTool == 0)
            {
                d->AddTriangleFilled(screen(end),
                                     screen({end.x - v.x - v.y * .45f, end.y - v.y + v.x * .45f}),
                                     screen({end.x - v.x + v.y * .45f, end.y - v.y - v.x * .45f}), color);
            }
            else
            {
                d->AddRectFilled(screen({end.x - 5, end.y - 5}), screen({end.x + 5, end.y + 5}), color);
            }
        }
    }
    if (tileTool != 1)
    {
        d->AddRectFilled(screen({center.x - 4, center.y - 4}), screen({center.x + 4, center.y + 4}),
                         IM_COL32(250, 230, 180, 255));
    }
    d->PopClipRect();
    return consumed;
}

// A transient face uses the same corner order and UV mapping as
// TileDocument.AddAtlasFace.
void tilePreviewData(float *data, Vector3 at, Vector2 size, int plane, const float *uv)
{
    float input[]{at.x, at.y, at.z, size.x, size.y, (float)plane};
    tileInteractionCallback(2, data, const_cast<float *>(uv), nullptr, input, nullptr);
}

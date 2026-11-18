// Screen-space picking and drag geometry. Xenon owns selection, records and Undo transactions.
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include <imgui.h>
#ifdef _WIN32
#define API extern "C" __declspec(dllexport)
#else
#define API extern "C" __attribute__((visibility("default")))
#endif
extern "C" Vector2 ed_pointer();
extern "C" int ed_mouse(int);
extern "C" int ed_pick(const float *, float, float, float, float, float, float, float, float, float, float *);
namespace
{
constexpr int stride = 16;
Camera3D camera{};
Rectangle viewport{};
bool drawn = false;
int shownMode = 0;
Vector2 framePress{};
bool framePressed = false;
Color colors[] = {{235, 85, 85, 255}, {103, 219, 137, 255}, {90, 160, 255, 255}};
// Display/test projection of the Xenon gesture; no transform algorithm lives here.
struct Drag
{
    float *record = nullptr;
    int mode = 0, handle = 0;
    Vector3 start{};
} drag;
using SceneGizmoCallback = int (*)(int, float *, float *, float *, float *);
SceneGizmoCallback sceneGizmoCallback = nullptr;
int dispatchGizmo(int operation, float *record, float x = 0, float y = 0, float mode = 0, float handle = 0,
                  bool snap = false)
{
    float context[]{camera.position.x, camera.position.y, camera.position.z, camera.target.x, camera.target.y,
                    camera.target.z,   camera.up.x,       camera.up.y,       camera.up.z,     camera.fovy,
                    viewport.x,        viewport.y,        viewport.width,    viewport.height};
    float input[]{x, y, mode, handle, snap ? 1.f : 0.f};
    float state[6]{};
    int result = sceneGizmoCallback(operation, record, input, context, state);
    if (operation == 1 && result)
    {
        drag.record = record;
    }
    if (!state[0])
    {
        drag.record = nullptr;
    }
    drag.mode = (int)state[1];
    drag.handle = (int)state[2];
    drag.start = {state[3], state[4], state[5]};
    return result;
}

Vector3 center(const float *r)
{
    return {r[1], r[2] + r[6] * .5f, r[3]};
}
Quaternion orientation(const float *r)
{
    return QuaternionFromEuler(r[12] * DEG2RAD, r[4] * DEG2RAD, r[13] * DEG2RAD);
}
Vector3 axis(int index)
{
    return index == 0 ? Vector3{1, 0, 0} : index == 1 ? Vector3{0, 1, 0} : Vector3{0, 0, 1};
}
Vector2 project(Vector3 p)
{
    return Vector2Add(GetWorldToScreenEx(p, camera, (int)viewport.width, (int)viewport.height),
                      {viewport.x, viewport.y});
}
Ray mouseRay(Vector2 p)
{
    return GetScreenToWorldRayEx(Vector2Subtract(p, {viewport.x, viewport.y}), camera, (int)viewport.width,
                                 (int)viewport.height);
}
Vector3 forward()
{
    return Vector3Normalize(Vector3Subtract(camera.target, camera.position));
}
float handleLength(Vector3 p)
{
    float depth = Vector3DotProduct(Vector3Subtract(p, camera.position), forward());
    return std::clamp(depth * std::tan(camera.fovy * DEG2RAD * .5f) * 2 * 92 / std::max(1.f, viewport.height),
                      .05f, 1000.f);
}
bool visible(Vector3 p)
{
    return viewport.width > 0 && Vector3DotProduct(Vector3Subtract(p, camera.position), forward()) > .05f;
}
void axesFor(const float *r, int mode, Vector3 *axes)
{
    for (int i = 0; i < 3; i++)
    {
        axes[i] = mode == 3 ? Vector3RotateByQuaternion(axis(i), orientation(r)) : axis(i);
    }
}
Vector3 ringPoint(Vector3 pivot, int index, float angle, float radius)
{
    return Vector3Add(pivot, Vector3Scale(Vector3Add(Vector3Scale(axis((index + 1) % 3), std::cos(angle)),
                                                     Vector3Scale(axis((index + 2) % 3), std::sin(angle))),
                                          radius));
}
int hover(const float *r, int mode, Vector2 mouse)
{
    return dispatchGizmo(0, const_cast<float *>(r), mouse.x, mouse.y, (float)mode);
}
bool begin(float *r, int mode, int handle, Vector2 mouse)
{
    return dispatchGizmo(1, r, mouse.x, mouse.y, (float)mode, (float)handle) != 0;
}
void applyTranslation(Vector3 delta, bool snap)
{
    dispatchGizmo(4, nullptr, delta.x, delta.y, delta.z, 0, snap);
}
void applyRotation(float angle, bool snap)
{
    dispatchGizmo(5, nullptr, angle, 0, 0, 0, snap);
}
void applyScale(float factor, bool snap)
{
    dispatchGizmo(6, nullptr, factor, 0, 0, 0, snap);
}
void update(Vector2 mouse, bool snap)
{
    dispatchGizmo(3, nullptr, mouse.x, mouse.y, 0, 0, snap);
}
int finish(bool cancel)
{
    return dispatchGizmo(2, nullptr, 0, 0, 0, 0, cancel);
}
void line(Vector3 a, Vector3 b, float width, Color color)
{
    DrawCylinderEx(a, b, width, width, 6, color);
}
} // namespace

API void ed_bind_scene_gizmo(SceneGizmoCallback callback)
{
    sceneGizmoCallback = callback;
}
API void ed_gizmo_init()
{
}
API void ed_gizmo_close()
{
    finish(true);
}
API void ed_gizmo_frame()
{
    framePressed = ed_mouse(1) != 0;
    ImVec2 mouse = ImGui::GetIO().MouseClickedPos[0];
    ImVec2 main = ImGui::GetMainViewport()->Pos;
    framePress = {mouse.x - main.x, mouse.y - main.y};
}
API void ed_gizmo_context(float x, float y, float z, float fx, float fy, float fz, float fov, float vx,
                          float vy, float width, float height)
{
    camera = {{x, y, z}, {x + fx, y + fy, z + fz}, {0, 1, 0}, fov, CAMERA_PERSPECTIVE};
    viewport = {vx, vy, width, height};
    drawn = false;
}
API int ed_gizmo_active()
{
    return drag.record != nullptr;
}
API int ed_gizmo_finish(int cancel)
{
    return finish(cancel != 0);
}
// 0: not consumed; 1: dragging; 2: commit; 3: unchanged release; 4: cancelled.
API int ed_gizmo_update(float *record, int mode, int snap, int cancel)
{
    Vector2 mouse = ed_pointer();
    float context[]{camera.position.x, camera.position.y, camera.position.z, camera.target.x, camera.target.y,
                    camera.target.z,   camera.up.x,       camera.up.y,       camera.up.z,     camera.fovy,
                    viewport.x,        viewport.y,        viewport.width,    viewport.height};
    float input[]{mouse.x,
                  mouse.y,
                  (float)mode,
                  0,
                  snap ? 1.f : 0.f,
                  cancel ? 1.f : 0.f,
                  ImGui::GetIO().AppFocusLost ? 1.f : 0.f,
                  framePressed ? 1.f : 0.f,
                  framePress.x,
                  framePress.y,
                  ImGui::IsMouseClicked(1) ? 1.f : 0.f,
                  ImGui::IsMouseDown(0) ? 1.f : 0.f};
    float state[6]{};
    int result = sceneGizmoCallback(7, record, input, context, state);
    framePressed = input[7] != 0;
    drag.record = state[0] != 0 ? record : nullptr;
    drag.mode = (int)state[1];
    drag.handle = (int)state[2];
    drag.start = {state[3], state[4], state[5]};
    return result;
}
API void ed_object_outline(const float *r)
{
    Vector3 points[8];
    Quaternion q = orientation(r);
    Vector3 pivot = center(r);
    for (int i = 0; i < 8; i++)
    {
        points[i] = Vector3Add(pivot, Vector3RotateByQuaternion({(i & 1 ? .5f : -.5f) * (r[5] + .02f),
                                                                 (i & 2 ? .5f : -.5f) * (r[6] + .02f),
                                                                 (i & 4 ? .5f : -.5f) * (r[7] + .02f)},
                                                                q));
    }
    for (int i = 0; i < 8; i++)
    {
        for (int bit = 1; bit <= 4; bit *= 2)
        {
            if (!(i & bit))
            {
                DrawLine3D(points[i], points[i | bit], {255, 200, 121, 255});
            }
        }
    }
}
API void ed_gizmo_draw(const float *r, int mode)
{
    Vector3 pivot = center(r);
    if (!visible(pivot))
    {
        return;
    }
    drawn = true;
    shownMode = mode;
    float length = handleLength(pivot);
    Vector3 axes[3];
    axesFor(r, mode, axes);
    int selected = drag.record ? drag.handle : hover(r, mode, ed_pointer());
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlDisableDepthMask();
    for (int a = 0; a < 3; a++)
    {
        Color color = selected == a + 1 ? Color{255, 218, 108, 255} : colors[a];
        if (mode == 2)
        {
            for (int i = 0; i < 96; i++)
            {
                line(ringPoint(pivot, a, (float)i * 2 * PI / 96, length),
                     ringPoint(pivot, a, (float)(i + 1) * 2 * PI / 96, length), length * .010f, color);
            }
        }
        else
        {
            Vector3 end = Vector3Add(pivot, Vector3Scale(axes[a], length));
            line(Vector3Add(pivot, Vector3Scale(axes[a], length * .15f)), end, length * .013f, color);
            if (mode == 1)
            {
                DrawCylinderEx(Vector3Add(pivot, Vector3Scale(axes[a], length * .82f)), end, length * .06f, 0,
                               12, color);
            }
            else
            {
                DrawCube(end, length * .13f, length * .13f, length * .13f, color);
            }
        }
    }
    if (mode == 1)
    {
        for (int a = 0; a < 3; a++)
        {
            int b = (a + 1) % 3;
            Vector3 p[4];
            for (int c = 0; c < 4; c++)
            {
                p[c] = Vector3Add(pivot,
                                  Vector3Add(Vector3Scale(axes[a], length * (c == 1 || c == 2 ? .43f : .22f)),
                                             Vector3Scale(axes[b], length * (c >= 2 ? .43f : .22f))));
            }
            Color color = selected == 4 + a ? Color{255, 218, 108, 130} : Fade(colors[(a + 2) % 3], .3f);
            DrawTriangle3D(p[0], p[1], p[2], color);
            DrawTriangle3D(p[2], p[1], p[0], color);
            DrawTriangle3D(p[0], p[2], p[3], color);
            DrawTriangle3D(p[3], p[2], p[0], color);
            for (int c = 0; c < 4; c++)
            {
                line(p[c], p[(c + 1) % 4], length * .006f, colors[(a + 2) % 3]);
            }
        }
    }
    if (mode != 2)
    {
        DrawCube(pivot, length * .10f, length * .10f, length * .10f, selected == 7 ? YELLOW : WHITE);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableDepthTest();
}
API const char *ed_gizmo_hint()
{
    if (!drawn)
    {
        return "";
    }
    return shownMode == 1   ? "MOVE / World axes | Ctrl: 0.25 m | Esc: cancel"
           : shownMode == 2 ? "ROTATE / World axes | Ctrl: 15 degrees | Esc: cancel"
                            : "SCALE / Local axes | Center: uniform | Ctrl: 10% | Esc: cancel";
}

API int ed_gizmo_tests()
{
    Camera3D savedCamera = camera;
    Rectangle savedViewport = viewport;
    camera = {{8, 7, 10}, {0, 1, 0}, {0, 1, 0}, 60, CAMERA_PERSPECTIVE};
    viewport = {0, 0, 1000, 700};
    std::array<float, stride> object{2, 0, 0, 0, 25, 2, 2, 2, 0, 0, 0, 2, 20, -15};
    auto initial = object;
    int failures = 0;
    auto check = [&](bool condition, const char *name) {
        if (!condition)
        {
            std::printf("FAIL: gizmo %s\n", name);
            failures++;
        }
    };
    for (int mode = 1; mode <= 3; mode++)
    {
        for (int a = 0; a < 3; a++)
        {
            object = initial;
            Vector3 pivot = center(object.data());
            float length = handleLength(pivot);
            Vector3 axes[3];
            axesFor(object.data(), mode, axes);
            Vector2 mouse = project(mode == 2 ? ringPoint(pivot, a, .68f, length)
                                              : Vector3Add(pivot, Vector3Scale(axes[a], length * .8f)));
            check(begin(object.data(), mode, a + 1, mouse), "begin drag");
            if (mode == 1)
            {
                update(project(Vector3Add(drag.start, Vector3Scale(axes[a], 1.13f))), true);
                check(std::fabs(object[1 + a] - 1.25f) < .002f, "axis-only snapped translation");
            }
            if (mode == 2)
            {
                applyRotation(.59f, true);
                Quaternion expected = QuaternionMultiply(QuaternionFromAxisAngle(axis(a), 30 * DEG2RAD),
                                                         orientation(initial.data()));
                Quaternion actual = orientation(object.data());
                check(std::fabs(expected.x * actual.x + expected.y * actual.y + expected.z * actual.z +
                                expected.w * actual.w) > .99999f,
                      "world rotation composition");
            }
            if (mode == 3)
            {
                applyScale(1.37f, true);
                check(std::fabs(object[5 + a] - 2.8f) < .002f, "local snapped scale");
                check(Vector3Distance(center(object.data()), pivot) < .001f, "fixed scale pivot");
            }
            check(finish(false) == 2, "changed drag transaction");
        }
    }
    object = initial;
    begin(object.data(), 1, 1, project({1, 1, 0}));
    applyTranslation({2, 0, 0}, false);
    check(finish(true) == 4 && object == initial, "Esc restores exact record");
    begin(object.data(), 1, 1, project({1, 1, 0}));
    check(finish(false) == 3, "click produces no Undo item");
    object = initial;
    check(begin(object.data(), 1, 1, project({1, 1, 0})), "begin unbounded translation");
    applyTranslation({20001, -20002, -20003}, false);
    check(object[1] == 20001 && object[2] == -20002 && object[3] == -20003,
          "translation beyond former coordinate limits");
    finish(true);
    object = initial;
    object[4] = 270;
    object[12] = 90;
    auto wrapped = object;
    begin(object.data(), 2, 1,
          project(ringPoint(center(object.data()), 0, .68f, handleLength(center(object.data())))));
    applyRotation(0, false);
    check(finish(false) == 3 && object == wrapped, "rotation click preserves noncanonical Euler angles");
    object = initial;
    object[0] = 5;
    begin(object.data(), 3, 1, project({1, 1, 0}));
    applyScale(-10, false);
    check(object[5] >= .01999f && object[5] == object[7], "positive cylinder radius on both axes");
    finish(false);
    object = initial;
    begin(object.data(), 3, 7, project(center(object.data())));
    applyScale(2, false);
    check(object[5] == 4 && object[6] == 4 && object[7] == 4, "uniform scale");
    finish(false);
    object = initial;
    Vector3 pivot = center(object.data());
    float length = handleLength(pivot);
    check(hover(object.data(), 1, project(Vector3Add(pivot, {length * .9f, 0, 0}))) == 1,
          "screen-space X handle picking");
    object = initial;
    check(begin(object.data(), 1, 4, project(Vector3Add(pivot, {length * .3f, length * .3f, 0}))),
          "XY plane drag begins");
    update(project(Vector3Add(drag.start, {1.13f, -.61f, 0})), true);
    check(std::fabs(object[1] - 1.25f) < .002f && std::fabs(object[2] + .5f) < .002f && object[3] == 0,
          "XY plane translation excludes Z");
    finish(false);
    float ray[] = {-6, 2, 0, 1, 0, 0};
    float distance = 0;
    check(ed_pick(ray, 0, 0, 0, 2, 4, 1, 0, 0, 90, &distance) && std::fabs(distance - 4) < .001f,
          "object picking uses full rotation and centered pivot");
    camera = savedCamera;
    viewport = savedViewport;
    if (!failures)
    {
        std::puts(
            "PASS: gizmo XYZ transforms, snapping, local scale, pivot, cancellation and screen picking.");
    }
    return failures;
}

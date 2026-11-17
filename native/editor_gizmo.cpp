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
constexpr int stride = 15;
Camera3D camera{};
Rectangle viewport{};
bool drawn = false;
int shownMode = 0;
Vector2 framePress{};
bool framePressed = false;
Color colors[] = {{235, 85, 85, 255}, {103, 219, 137, 255}, {90, 160, 255, 255}};
struct Drag
{
    float *record = nullptr;
    std::array<float, stride> before{};
    int mode = 0, handle = 0;
    Vector3 pivot{}, direction{}, plane{}, start{}, axes[3]{};
    Vector2 mouse{};
    float length = 1, angle = 0;
    bool screenRotation = false;
} drag;

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
bool intersect(Ray ray, Vector3 origin, Vector3 normal, Vector3 &hit)
{
    float denominator = Vector3DotProduct(ray.direction, normal);
    if (std::fabs(denominator) < .0001f)
    {
        return false;
    }
    float t = Vector3DotProduct(Vector3Subtract(origin, ray.position), normal) / denominator;
    if (!std::isfinite(t) || t < 0 || t > 100000)
    {
        return false;
    }
    hit = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
    return true;
}
float segmentDistance(Vector2 p, Vector2 a, Vector2 b)
{
    Vector2 d = Vector2Subtract(b, a);
    float length = Vector2LengthSqr(d);
    if (length < .001f)
    {
        return Vector2Distance(p, a);
    }
    float t = std::clamp(Vector2DotProduct(Vector2Subtract(p, a), d) / length, 0.f, 1.f);
    return Vector2Distance(p, Vector2Add(a, Vector2Scale(d, t)));
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
    Vector3 pivot = center(r);
    if (!visible(pivot) || !CheckCollisionPointRec(mouse, viewport))
    {
        return 0;
    }
    float length = handleLength(pivot);
    Vector3 axes[3];
    axesFor(r, mode, axes);
    if (mode != 2 && Vector2Distance(mouse, project(pivot)) < 8)
    {
        return 7;
    }
    int result = 0;
    float best = 9;
    if (mode == 2)
    {
        for (int a = 0; a < 3; a++)
        {
            Vector2 previous = project(ringPoint(pivot, a, 0, length));
            for (int i = 1; i <= 96; i++)
            {
                Vector2 next = project(ringPoint(pivot, a, (float)i * 2 * PI / 96, length));
                float distance = segmentDistance(mouse, previous, next);
                if (distance < best)
                {
                    best = distance;
                    result = a + 1;
                }
                previous = next;
            }
        }
        return result;
    }
    for (int a = 0; a < 3; a++)
    {
        Vector2 start = project(Vector3Add(pivot, Vector3Scale(axes[a], length * .18f)));
        Vector2 end = project(Vector3Add(pivot, Vector3Scale(axes[a], length)));
        if (Vector2Distance(start, end) < 16)
        {
            continue;
        }
        float distance = segmentDistance(mouse, start, end);
        if (distance < best)
        {
            best = distance;
            result = a + 1;
        }
    }
    if (result || mode != 1)
    {
        return result;
    }
    for (int a = 0; a < 3; a++)
    {
        int b = (a + 1) % 3;
        Vector3 hit;
        if (!intersect(mouseRay(mouse), pivot, Vector3CrossProduct(axes[a], axes[b]), hit))
        {
            continue;
        }
        Vector3 local = Vector3Scale(Vector3Subtract(hit, pivot), 1 / length);
        float x = Vector3DotProduct(local, axes[a]), y = Vector3DotProduct(local, axes[b]);
        if (x >= .22f && x <= .43f && y >= .22f && y <= .43f)
        {
            return 4 + a;
        }
    }
    return 0;
}
bool begin(float *r, int mode, int handle, Vector2 mouse)
{
    drag = {};
    drag.record = r;
    drag.mode = mode;
    drag.handle = handle;
    std::copy(r, r + stride, drag.before.begin());
    drag.pivot = center(r);
    drag.length = handleLength(drag.pivot);
    drag.mouse = mouse;
    axesFor(r, mode, drag.axes);
    if (handle <= 3)
    {
        drag.direction = drag.axes[handle - 1];
        drag.plane =
            mode == 2
                ? drag.direction
                : Vector3Subtract(forward(),
                                  Vector3Scale(drag.direction, Vector3DotProduct(forward(), drag.direction)));
        if (Vector3LengthSqr(drag.plane) < .0001f)
        {
            drag = {};
            return false;
        }
        drag.plane = Vector3Normalize(drag.plane);
    }
    else if (handle < 7)
    {
        int a = handle - 4;
        drag.plane = Vector3CrossProduct(drag.axes[a], drag.axes[(a + 1) % 3]);
    }
    else
    {
        drag.plane = forward();
    }
    if (mode == 2 && std::fabs(Vector3DotProduct(drag.plane, mouseRay(mouse).direction)) < .12f)
    {
        // Edge-on rings cannot define a stable ray/plane intersection. Use their projected tangent.
        drag.screenRotation = true;
    }
    if (!drag.screenRotation && !(mode == 3 && handle == 7) &&
        !intersect(mouseRay(mouse), drag.pivot, drag.plane, drag.start))
    {
        drag = {};
        return false;
    }
    return true;
}
float snapped(float value, float step, bool snap)
{
    return snap ? std::round(value / step) * step : value;
}
void applyTranslation(Vector3 delta, bool snap)
{
    float *r = drag.record;
    r[1] = std::clamp(drag.before[1] + snapped(delta.x, .25f, snap), -10000.f, 10000.f);
    r[2] = std::clamp(drag.before[2] + snapped(delta.y, .25f, snap), -10000.f, 10000.f);
    r[3] = std::clamp(drag.before[3] + snapped(delta.z, .25f, snap), -10000.f, 10000.f);
}
void applyRotation(float angle, bool snap)
{
    angle = snapped(angle, 15 * DEG2RAD, snap);
    if (std::fabs(angle) < .0000001f)
    {
        // Keep the author's Euler representation on a click or a snapped return to zero.
        drag.record[12] = drag.before[12];
        drag.record[4] = drag.before[4];
        drag.record[13] = drag.before[13];
        return;
    }
    Quaternion rotation =
        QuaternionMultiply(QuaternionFromAxisAngle(drag.direction, angle), orientation(drag.before.data()));
    Vector3 euler = QuaternionToEuler(QuaternionNormalize(rotation));
    drag.record[12] = euler.x * RAD2DEG;
    drag.record[4] = euler.y * RAD2DEG;
    drag.record[13] = euler.z * RAD2DEG;
}
void applyScale(float factor, bool snap)
{
    factor = 1 + snapped(factor - 1, .1f, snap);
    bool affected[3]{};
    int kind = (int)drag.before[0];
    if (drag.handle == 7 || kind >= 7)
    {
        affected[0] = affected[1] = affected[2] = true;
    }
    else
    {
        affected[drag.handle - 1] = true;
        if (kind == 5 && drag.handle != 2)
        {
            affected[0] = affected[2] = true;
        }
    }
    float low = 0, high = 1e8f;
    for (int a = 0; a < 3; a++)
    {
        if (affected[a])
        {
            low = std::max(low, .02f / drag.before[5 + a]);
            high = std::min(high, 1000.f / drag.before[5 + a]);
        }
    }
    factor = std::clamp(factor, low, high);
    for (int a = 0; a < 3; a++)
    {
        drag.record[5 + a] = std::clamp(drag.before[5 + a] * (affected[a] ? factor : 1), .02f, 1000.f);
    }
    // Authoring records store the unrotated lower face; manipulating scale keeps the visual pivot fixed.
    drag.record[2] = factor == 1 ? drag.before[2] : drag.pivot.y - drag.record[6] * .5f;
}
void update(Vector2 mouse, bool snap)
{
    Vector3 hit{};
    if (drag.mode == 2)
    {
        if (drag.screenRotation)
        {
            Vector2 screen = project(drag.pivot);
            Vector2 radial = Vector2Normalize(Vector2Subtract(drag.mouse, screen));
            Vector2 tangent{-radial.y, radial.x};
            float sign = Vector3DotProduct(drag.direction, forward()) > 0 ? -1.f : 1.f;
            drag.angle = sign * Vector2DotProduct(Vector2Subtract(mouse, drag.mouse), tangent) * .015f;
        }
        else if (intersect(mouseRay(mouse), drag.pivot, drag.plane, hit))
        {
            Vector3 a = Vector3Normalize(Vector3Subtract(drag.start, drag.pivot));
            Vector3 b = Vector3Normalize(Vector3Subtract(hit, drag.pivot));
            float next = std::atan2(Vector3DotProduct(drag.direction, Vector3CrossProduct(a, b)),
                                    Vector3DotProduct(a, b));
            drag.angle += next;
            drag.start = hit;
        }
        applyRotation(drag.angle, snap);
        return;
    }
    if (drag.mode == 3 && drag.handle == 7)
    {
        applyScale(std::exp((mouse.x - drag.mouse.x - mouse.y + drag.mouse.y) * .008f), snap);
        return;
    }
    if (!intersect(mouseRay(mouse), drag.pivot, drag.plane, hit))
    {
        return;
    }
    Vector3 delta = Vector3Subtract(hit, drag.start);
    if (drag.mode == 3)
    {
        applyScale(1 + Vector3DotProduct(delta, drag.direction) / drag.length, snap);
    }
    else
    {
        if (drag.handle <= 3)
        {
            delta = Vector3Scale(drag.direction, Vector3DotProduct(delta, drag.direction));
        }
        applyTranslation(delta, snap);
    }
}
int finish(bool cancel)
{
    if (!drag.record)
    {
        return 0;
    }
    bool changed = false;
    for (int i = 0; i < stride; i++)
    {
        if (std::fabs(drag.record[i] - drag.before[i]) > .00001f)
        {
            changed = true;
        }
    }
    if (cancel)
    {
        std::copy(drag.before.begin(), drag.before.end(), drag.record);
    }
    drag = {};
    return cancel ? 4 : changed ? 2 : 3;
}
void line(Vector3 a, Vector3 b, float width, Color color)
{
    DrawCylinderEx(a, b, width, width, 6, color);
}
} // namespace

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
    if (drag.record)
    {
        if (cancel || ImGui::GetIO().AppFocusLost || drag.record != record || drag.mode != mode ||
            ImGui::IsMouseClicked(1))
        {
            return finish(true);
        }
        // Mouse can leave the viewport during a drag. Release is still observed globally.
        update(ed_pointer(), snap != 0);
        if (!ImGui::IsMouseDown(0))
        {
            return finish(false);
        }
        return 1;
    }
    if (cancel || !record || mode < 1 || mode > 3 || !framePressed)
    {
        return 0;
    }
    framePressed = false;
    int handle = hover(record, mode, framePress);
    if (!handle || !begin(record, mode, handle, framePress))
    {
        return 0;
    }
    update(ed_pointer(), snap != 0);
    return ImGui::IsMouseDown(0) ? 1 : finish(false);
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

extern "C" int ox_model_triangles(int, float, float, float, float *, int);
int tileModelTests(int model)
{
    int failures = 0;
    float triangles[18]{};
    if (ox_model_triangles(model, 4, 6, .02f, nullptr, 0) != 2 ||
        ox_model_triangles(model, 4, 6, .02f, triangles, 2) != 2)
    {
        failures++;
    }
    for (int i = 0; i < 6; i++)
    {
        if (fabsf(fabsf(triangles[i * 3]) - 2) > .001f || fabsf(fabsf(triangles[i * 3 + 1]) - 3) > .001f ||
            fabsf(triangles[i * 3 + 2]) > .001f)
        {
            failures++;
        }
    }
    RenderTexture2D target = LoadRenderTexture(48, 48);
    int red[2]{};
    for (int pass = 0; pass < 2; pass++)
    {
        float ambient = pass == 0 ? .2f : .8f;
        ox_lights_reset(ambient, ambient, ambient);
        BeginTextureMode(target);
        ClearBackground(BLACK);
        Camera3D camera{{0, 1, 4}, {0, 1, 0}, {0, 1, 0}, 45, CAMERA_PERSPECTIVE};
        BeginMode3D(camera);
        ox_draw_model(model, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0xFFFFFFFFu);
        EndMode3D();
        EndTextureMode();
        Image capture = LoadImageFromTexture(target.texture);
        red[pass] = GetImageColor(capture, 24, 24).r;
        UnloadImage(capture);
    }
    UnloadRenderTexture(target);
    if (red[0] < 40 || red[0] > 65 || red[1] < 190 || red[1] > 220)
    {
        failures++;
    }
    puts(failures
             ? "FAIL: model collider export / standard material."
             : "PASS: model triangle centering/scaling and standard material responds to scene lighting.");
    return failures;
}

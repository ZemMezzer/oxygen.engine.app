// Keep Win32 names out of the raylib translation unit.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <commdlg.h>
#include <shlobj.h>
#include <string>

using EditorMenuPolicy = int (*)(int, int, int, int, int);
static EditorMenuPolicy menuPolicy = nullptr;
extern "C" __declspec(dllexport) void ed_bind_menu_policy(EditorMenuPolicy callback)
{
    menuPolicy = callback;
}
namespace
{
HWND owner = nullptr;
WNDPROC previousProcedure = nullptr;
HMENU menuBar = nullptr;
int menuCommand = 0;
LRESULT CALLBACK editorProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_COMMAND && HIWORD(wParam) == 0 && LOWORD(wParam) >= 1000 && LOWORD(wParam) < 1300)
    {
        menuCommand = LOWORD(wParam) - 1000;
        return 0;
    }
    return CallWindowProcW(previousProcedure, window, message, wParam, lParam);
}
void item(HMENU menu, const wchar_t *name, int action)
{
    AppendMenuW(menu, MF_STRING, 1000 + action, name);
}
void section(const wchar_t *name, HMENU menu)
{
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)menu, name);
}

std::wstring wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring result(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, result.data(), n);
    if (!result.empty())
    {
        result.pop_back();
    }
    return result;
}
int output(const wchar_t *path, char *out, int size)
{
    return WideCharToMultiByte(CP_UTF8, 0, path, -1, out, size, nullptr, nullptr) > 0;
}
} // namespace
extern "C" void ed_native_init()
{

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
}
extern "C" void ed_native_close()
{
    if (owner && IsWindow(owner))
    {
        SetMenu(owner, nullptr);
        if (previousProcedure)
        {
            SetWindowLongPtrW(owner, GWLP_WNDPROC, (LONG_PTR)previousProcedure);
        }
    }
    if (menuBar)
    {
        DestroyMenu(menuBar);
    }
    owner = nullptr;
    menuBar = nullptr;
    CoUninitialize();
}
extern "C" int ed_native_folder(const char *title, char *out, int size)
{
    auto caption = wide(title);
    BROWSEINFOW info{};
    info.hwndOwner = owner;
    info.lpszTitle = caption.c_str();
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&info);
    if (!id)
    {
        return 0;
    }
    wchar_t path[MAX_PATH]{};
    bool ok = SHGetPathFromIDListW(id, path) != 0;
    CoTaskMemFree(id);
    return ok && output(path, out, size);
}
extern "C" int ed_native_file(int save, int scene, const char *initial, char *out, int size)
{
    wchar_t file[32768]{};
    if (save)
    {
        wcscpy_s(file, L"Main.oxmap");
    }
    auto folder = wide(initial);
    OPENFILENAMEW info{};
    info.lStructSize = sizeof(info);
    info.hwndOwner = owner;
    info.lpstrFile = file;
    info.nMaxFile = 32768;
    info.lpstrInitialDir = folder.c_str();
    info.lpstrDefExt = scene ? L"oxmap" : nullptr;
    info.lpstrFilter =
        scene
            ? L"Oxygen tile scene\0*.oxmap\0\0"
            : L"Resources\0*.obj;*.gltf;*.glb;*.png;*.jpg;*.wav;*.ogg;*.mp3;*.flac;*.mtl;*.bin\0All files\0*.*\0\0";
    info.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    return (save ? GetSaveFileNameW(&info) : GetOpenFileNameW(&info)) && output(file, out, size);
}
extern "C" int ed_native_guard()
{
    int result =
        MessageBoxW(owner, L"Save changes before continuing?\nYes: save. No: discard. Cancel: keep editing.",
                    L"Oxygen Editor", MB_YESNOCANCEL | MB_ICONQUESTION);
    return result == IDYES ? 2 : result == IDNO ? 1 : 0;
}
extern "C" int ed_native_replace(const char *from, const char *to)
{
    return MoveFileExW(wide(from).c_str(), wide(to).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

extern "C" void ed_native_menu(void *window)
{
    owner = (HWND)window;
    menuBar = CreateMenu();
    HMENU file = CreatePopupMenu();
    item(file, L"New Project...", 1);
    item(file, L"Open Project...", 2);
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    item(file, L"New Scene", 5);
    item(file, L"Open Scene...", 4);
    item(file, L"Save\tCtrl+S", 3);
    item(file, L"Save As...", 13);
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    item(file, L"Exit", 99);
    section(L"&File", file);
    HMENU edit = CreatePopupMenu();
    item(edit, L"Undo\tCtrl+Z", 7);
    item(edit, L"Redo\tCtrl+Y", 8);
    item(edit, L"Duplicate\tCtrl+D", 91);
    item(edit, L"Delete\tDel", 50);
    section(L"&Edit", edit);
    HMENU assets = CreatePopupMenu();
    item(assets, L"Import File...", 9);
    item(assets, L"Import Folder...", 10);
    section(L"&Assets", assets);
    HMENU scene = CreatePopupMenu();
    item(scene, L"Play / Stop\tF5", 6);
    item(scene, L"Focus Selected\tF", 64);
    section(L"&Scene", scene);
    HMENU windows = CreatePopupMenu();
    item(windows, L"Scene", 200);
    item(windows, L"Hierarchy", 201);
    item(windows, L"Inspector", 202);
    item(windows, L"Resources", 203);
    item(windows, L"Tiles", 204);
    item(windows, L"Layers", 205);
    AppendMenuW(windows, MF_SEPARATOR, 0, nullptr);
    item(windows, L"Reset Layout", 210);
    section(L"&Window", windows);
    SetMenu(owner, menuBar);
    previousProcedure = (WNDPROC)SetWindowLongPtrW(owner, GWLP_WNDPROC, (LONG_PTR)editorProcedure);
    DrawMenuBar(owner);
}
extern "C" int ed_native_command()
{
    int value = menuCommand;
    menuCommand = 0;
    return value;
}
extern "C" void ed_native_menu_state(int play, int selected, int undo, int redo)
{
    for (int action : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 13, 50, 64, 91, 99, 200, 201, 202, 203, 204, 205, 210})
    {
        EnableMenuItem(menuBar, 1000 + action,
                       MF_BYCOMMAND |
                           (menuPolicy(action, play, selected, undo, redo) ? MF_ENABLED : MF_GRAYED));
    }
}

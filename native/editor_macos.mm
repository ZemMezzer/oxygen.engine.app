// Native macOS menus and file panels. Selected by CMake on Apple platforms.
#import <Cocoa/Cocoa.h>
#include <cstdio>
#include <cstring>

static int pendingCommand = 0;
static bool playing = false, selected = false, canUndo = false, canRedo = false;
@interface OxygenMenuTarget : NSObject <NSMenuItemValidation>
- (void)performEditorCommand:(NSMenuItem *)sender;
@end
@implementation OxygenMenuTarget
- (void)performEditorCommand:(NSMenuItem *)sender
{
    pendingCommand = (int)sender.tag;
}
- (BOOL)validateMenuItem:(NSMenuItem *)item
{
    int action = (int)item.tag;
    if (action == 7)
    {
        return !playing && canUndo;
    }
    if (action == 8)
    {
        return !playing && canRedo;
    }
    if (action == 50 || action == 64 || action == 91)
    {
        return !playing && selected;
    }
    if (action >= 200 || action == 99 || action == 6)
    {
        return YES;
    }
    return !playing;
}
@end
static OxygenMenuTarget *menuTarget;
static NSMenu *previousMenu;

static void item(NSMenu *menu, NSString *title, int action)
{
    NSMenuItem *entry = [[NSMenuItem alloc] initWithTitle:title
                                                   action:@selector(performEditorCommand:)
                                            keyEquivalent:@""];
    entry.target = menuTarget;
    entry.tag = action;
    [menu addItem:entry];
    [entry release];
}
static NSMenu *section(NSMenu *bar, NSString *name)
{
    NSMenuItem *entry = [[NSMenuItem alloc] initWithTitle:name action:nil keyEquivalent:@""];
    NSMenu *menu = [[NSMenu alloc] initWithTitle:name];
    entry.submenu = menu;
    [bar addItem:entry];
    [entry release];
    [menu release];
    return menu;
}
static int pathResult(NSURL *url, char *out, int size)
{
    if (!url)
    {
        return 0;
    }
    const char *path = url.path.UTF8String;
    if (!path || std::strlen(path) >= (size_t)size)
    {
        return 0;
    }
    std::strcpy(out, path);
    return 1;
}
extern "C" void ed_native_init()
{
}
extern "C" void ed_native_menu(void *)
{
    previousMenu = [NSApp.mainMenu retain];
    menuTarget = [[OxygenMenuTarget alloc] init];
    NSMenu *bar = [[NSMenu alloc] init];
    NSMenu *app = section(bar, @"Oxygen");
    item(app, @"Quit Oxygen", 99);
    NSMenu *file = section(bar, @"File");
    item(file, @"New Project…", 1);
    item(file, @"Open Project…", 2);
    [file addItem:NSMenuItem.separatorItem];
    item(file, @"New Scene", 5);
    item(file, @"Open Scene…", 4);
    item(file, @"Save", 3);
    item(file, @"Save As…", 13);
    NSMenu *edit = section(bar, @"Edit");
    item(edit, @"Undo", 7);
    item(edit, @"Redo", 8);
    item(edit, @"Duplicate", 91);
    item(edit, @"Delete", 50);
    NSMenu *assets = section(bar, @"Assets");
    item(assets, @"Import File…", 9);
    item(assets, @"Import Folder…", 10);
    NSMenu *scene = section(bar, @"Scene");
    item(scene, @"Play / Stop", 6);
    item(scene, @"Focus Selected", 64);
    NSMenu *windows = section(bar, @"Window");
    item(windows, @"Scene", 200);
    item(windows, @"Hierarchy", 201);
    item(windows, @"Inspector", 202);
    item(windows, @"Resources", 203);
    item(windows, @"Tiles", 204);
    [windows addItem:NSMenuItem.separatorItem];
    item(windows, @"Reset Layout", 210);
    [NSApp setMainMenu:bar];
    [bar release];
}
extern "C" int ed_native_command()
{
    int result = pendingCommand;
    pendingCommand = 0;
    return result;
}
extern "C" void ed_native_menu_state(int play, int selection, int undo, int redo)
{
    playing = play;
    selected = selection;
    canUndo = undo;
    canRedo = redo;
}
extern "C" void ed_native_close()
{
    [NSApp setMainMenu:previousMenu];
    [previousMenu release];
    previousMenu = nil;
    [menuTarget release];
    menuTarget = nil;
}
extern "C" int ed_native_folder(const char *title, char *out, int size)
{
    NSOpenPanel *panel = NSOpenPanel.openPanel;
    panel.title = [NSString stringWithUTF8String:title];
    panel.canChooseDirectories = YES;
    panel.canChooseFiles = NO;
    panel.canCreateDirectories = YES;
    return [panel runModal] == NSModalResponseOK && pathResult(panel.URL, out, size);
}
extern "C" int ed_native_file(int save, int scene, const char *initial, char *out, int size)
{
    NSSavePanel *panel = save ? NSSavePanel.savePanel : NSOpenPanel.openPanel;
    panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:initial]];
    if (scene)
    {
        panel.allowedFileTypes = @[ @"oxmap" ];
    }
    if (save)
    {
        panel.nameFieldStringValue = @"Main.oxmap";
    }
    return [panel runModal] == NSModalResponseOK && pathResult(panel.URL, out, size);
}
extern "C" int ed_native_guard()
{
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Save changes before continuing?";
    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Discard"];
    [alert addButtonWithTitle:@"Cancel"];
    NSModalResponse response = [alert runModal];
    [alert release];
    return response == NSAlertFirstButtonReturn ? 2 : response == NSAlertSecondButtonReturn ? 1 : 0;
}
extern "C" int ed_native_replace(const char *from, const char *to)
{
    return std::rename(from, to) == 0;
}

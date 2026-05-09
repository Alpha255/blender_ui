/**
 * Blender Menu System Demo
 * Registers File / Edit / Render / Window / Help menus matching Blender's topbar.
 * Source reference: scripts/startup/bl_ui/space_topbar.py
 */

#include <bl_ui/menu_type.h>
#include <bl_ui/icons.h>
#include "../src/ui/app.h"
#include <iostream>
#include <memory>

using namespace bl_ui;

// ---------------------------------------------------------------------------
// Helper: make a unique_ptr<MenuType> with idname + label + draw
// ---------------------------------------------------------------------------

template<typename DrawFn>
static std::unique_ptr<MenuType> make_menu(std::string idname,
                                           std::string label,
                                           DrawFn draw) {
    auto mt  = std::make_unique<MenuType>();
    mt->idname = std::move(idname);
    mt->label  = std::move(label);
    mt->draw   = std::move(draw);
    return mt;
}

// ---------------------------------------------------------------------------
// Menu definitions
// ---------------------------------------------------------------------------

static void register_menus(MenuRegistry& reg) {

    // ------ File ------
    reg.add(make_menu("TOPBAR_MT_file", "File", [](const Context&, Menu& m) {
        m.layout.operator_("wm.read_homefile",    "New",              ICON_FILE_NEW,  "Ctrl N");
        m.layout.operator_("wm.open_mainfile",    "Open...",          ICON_FILEBROWSER,"Ctrl O");
        m.layout.operator_("wm.recover_auto_save","Recover Auto Save...");
        m.layout.separator();
        m.layout.operator_("wm.save_mainfile",    "Save",             0,              "Ctrl S");
        m.layout.operator_("wm.save_as_mainfile", "Save As...",       0,              "Ctrl Shift S");
        m.layout.operator_("wm.save_as_mainfile", "Save Copy...");
        m.layout.separator();
        m.layout.menu("TOPBAR_MT_file_import",    "Import",           ICON_IMPORT);
        m.layout.menu("TOPBAR_MT_file_export",    "Export",           ICON_EXPORT);
        m.layout.separator();
        m.layout.operator_("wm.link",             "Link...");
        m.layout.operator_("wm.append",           "Append...");
        m.layout.separator();
        m.layout.menu("TOPBAR_MT_file_external_data", "External Data",ICON_FILE_FOLDER);
        m.layout.separator();
        m.layout.operator_("wm.quit_blender",     "Quit",             ICON_QUIT,      "Ctrl Q");
    }));

    // File submenus
    reg.add(make_menu("TOPBAR_MT_file_import", "Import", [](const Context&, Menu& m) {
        m.layout.label("Import");
        m.layout.separator();
        m.layout.operator_("import_scene.obj",    "Wavefront (.obj)");
        m.layout.operator_("import_scene.fbx",    "FBX (.fbx)");
        m.layout.operator_("import_scene.gltf",   "glTF 2.0 (.glb/.gltf)");
    }));

    reg.add(make_menu("TOPBAR_MT_file_export", "Export", [](const Context&, Menu& m) {
        m.layout.label("Export");
        m.layout.separator();
        m.layout.operator_("export_scene.obj",    "Wavefront (.obj)");
        m.layout.operator_("export_scene.fbx",    "FBX (.fbx)");
        m.layout.operator_("export_scene.gltf",   "glTF 2.0 (.glb/.gltf)");
    }));

    reg.add(make_menu("TOPBAR_MT_file_external_data", "External Data", [](const Context&, Menu& m) {
        m.layout.operator_("file.pack_all",       "Pack Resources");
        m.layout.operator_("file.unpack_all",     "Unpack Resources");
        m.layout.separator();
        m.layout.operator_("file.make_paths_relative", "Make Paths Relative");
        m.layout.operator_("file.make_paths_absolute", "Make Paths Absolute");
        m.layout.separator();
        m.layout.operator_("file.report_missing_files", "Report Missing Files");
        m.layout.operator_("file.find_missing_files",   "Find Missing Files");
    }));

    // ------ Edit ------
    reg.add(make_menu("TOPBAR_MT_edit", "Edit", [](const Context&, Menu& m) {
        m.layout.operator_("ed.undo",              "Undo",                    0, "Ctrl Z");
        m.layout.operator_("ed.redo",              "Redo",                    0, "Ctrl Shift Z");
        m.layout.operator_("ed.undo_history",      "Undo History...");
        m.layout.separator();
        m.layout.operator_("screen.repeat_last",   "Repeat Last",             0, "Shift R");
        m.layout.operator_("screen.repeat_history","Repeat History...");
        m.layout.separator();
        m.layout.operator_("screen.redo_last",     "Adjust Last Operation",   0, "F9");
        m.layout.separator();
        m.layout.menu("TOPBAR_MT_edit_menu_item",  "Menu Search");
        m.layout.separator();
        m.layout.operator_("wm.operator_defaults", "Operator Defaults");
        m.layout.separator();
        m.layout.operator_("wm.open_preferences",  "Preferences...");
    }));

    reg.add(make_menu("TOPBAR_MT_edit_menu_item", "Menu Search", [](const Context&, Menu& m) {
        m.layout.operator_("wm.search_menu",      "Search...");
    }));

    // ------ Render ------
    reg.add(make_menu("TOPBAR_MT_render", "Render", [](const Context&, Menu& m) {
        m.layout.operator_("render.render",            "Render Image",            ICON_RENDER_STILL,      "F12");
        m.layout.operator_("render.render",            "Render Animation",        ICON_RENDER_ANIMATION,  "Ctrl F12");
        m.layout.operator_("sound.mixdown",            "Render Audio...",         ICON_OUTPUT);
        m.layout.separator();
        m.layout.operator_("render.view_show",         "View Render",             0,                      "F11");
        m.layout.operator_("render.play_rendered_anim","View Animation",          0,                      "Ctrl F11");
        m.layout.separator();
        m.layout.operator_("render.opengl",            "Viewport Render Image");
        m.layout.operator_("render.opengl",            "Viewport Render Animation");
    }));

    // ------ Window ------
    reg.add(make_menu("TOPBAR_MT_window", "Window", [](const Context&, Menu& m) {
        m.layout.operator_("wm.window_new",       "New Window",                      ICON_WINDOW);
        m.layout.operator_("wm.window_new_main",  "New Main Window",                 ICON_WINDOW);
        m.layout.separator();
        m.layout.operator_("wm.window_fullscreen_toggle", "Toggle Window Fullscreen", ICON_FULLSCREEN_ENTER);
        m.layout.separator();
        m.layout.operator_("screen.workspace_cycle", "Next Workspace");
        m.layout.operator_("screen.workspace_cycle", "Previous Workspace");
        m.layout.separator();
        m.layout.operator_("screen.screenshot",   "Save Screenshot");
    }));

    // ------ Help ------
    reg.add(make_menu("TOPBAR_MT_help", "Help", [](const Context&, Menu& m) {
        m.layout.operator_("wm.url_open",         "Manual",                          ICON_HELP);
        m.layout.operator_("wm.url_open",         "Blender Website",                 ICON_URL);
        m.layout.operator_("wm.url_open",         "Release Notes");
        m.layout.separator();
        m.layout.operator_("wm.url_open",         "Blender Store");
        m.layout.operator_("wm.url_open",         "Development Fund");
        m.layout.separator();
        m.layout.operator_("wm.url_open",         "Report a Bug",                    ICON_ERROR);
        m.layout.separator();
        m.layout.operator_("wm.sysinfo",          "Save System Info");
        m.layout.operator_("wm.splash",           "Splash Screen");
        m.layout.operator_("wm.splash_about",     "About");
    }));
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    App app(1280, 720, "Blender Menu System Demo");
    if (!app.ready()) {
        std::cerr << "Failed to initialize application.\n";
        return 1;
    }

    // Register all menus
    register_menus(app.registry());

    // Build the menu bar (same order as Blender's topbar)
    app.menu_bar().add_menu("File",   "TOPBAR_MT_file");
    app.menu_bar().add_menu("Edit",   "TOPBAR_MT_edit");
    app.menu_bar().add_menu("Render", "TOPBAR_MT_render");
    app.menu_bar().add_menu("Window", "TOPBAR_MT_window");
    app.menu_bar().add_menu("Help",   "TOPBAR_MT_help");

    // Operator callback — just log to console
    app.set_operator_callback([](const std::string& op) {
        std::cout << "[Demo] Operator activated: " << op << "\n";
    });

    app.run();
    return 0;
}

#pragma once

namespace bl_ui {

// A minimal subset of Blender's ICON_* enum values.
// Integer values match the sequential position in Blender's UI_icons.hh so
// that icon names are interchangeable with Blender's own constants.
enum BLIcon {
    ICON_NONE            = 0,

    // Dialogs / generic
    ICON_CANCEL          = 98,   // cancel.svg
    ICON_ERROR           = 99,   // error.svg
    ICON_FILE_NEW        = 123,  // file_new.svg
    ICON_QUIT            = 145,  // quit.svg

    // File operations
    ICON_FILEBROWSER     = 193,  // filebrowser.svg
    ICON_FILE_FOLDER     = 752,  // file_folder.svg
    ICON_EXPORT          = 762,  // export.svg
    ICON_IMPORT          = 787,  // import.svg

    // Edit
    ICON_COPYDOWN        = 200,  // copydown.svg
    ICON_PASTEDOWN       = 201,  // pastedown.svg
    ICON_PREFERENCES     = 219,  // preferences.svg

    // Render / view
    ICON_RENDER_STILL    = 350,  // render_still.svg
    ICON_RENDER_ANIMATION= 351,  // render_animation.svg
    ICON_OUTPUT          = 352,  // output.svg

    // Window
    ICON_FULLSCREEN_ENTER= 500,  // fullscreen_enter.svg
    ICON_FULLSCREEN_EXIT = 501,  // fullscreen_exit.svg
    ICON_WINDOW          = 502,  // window.svg

    // Help
    ICON_HELP            = 600,  // help.svg
    ICON_URL             = 601,  // url.svg
};

} // namespace bl_ui

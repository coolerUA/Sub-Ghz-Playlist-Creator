#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/popup.h>
#include <gui/modules/text_input.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/file_browser.h>
#include <storage/storage.h>
#define MAX_TEXT_LENGTH 128
// Most files one playlist can hold (each one uses a bit of the Flipper's small memory)
#define MAX_PLAYLIST_ENTRIES 300

// What the browser is used for
typedef enum {
    BrowserModeCreate, // "Create": pick .sub files/folder for a new playlist
    BrowserModeAddToEdit, // "[+] Add folder" while editing a playlist
    BrowserModePlaylists, // "Edit": pick a playlist (long OK = Rename / Delete)
} BrowserMode;

// Forward declare struct for callback
struct SubGhzPlaylistCreator;
typedef void (*SceneFileBrowserSelectCallback)(struct SubGhzPlaylistCreator* app, const char* path);

// Forward declare VariableItemList and DialogEx for struct members.
struct VariableItemList;
struct DialogEx;

// Add ReturnScene enum before struct definition
typedef enum {
    ReturnScene_None = 0,
    ReturnScene_Menu,
    ReturnScene_PlaylistEdit,
    ReturnScene_TextInput,
} ReturnScene;

typedef struct SubGhzPlaylistCreator {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    Submenu* playlist_edit_submenu;
    Popup* popup;
    TextInput* text_input;
    DialogEx* dialog;
    FileBrowser* file_browser;
    FuriString* file_browser_result;
    FuriString* playlist_name;
    FuriString* playlist_path;
    char text_buffer[MAX_TEXT_LENGTH];
    SceneFileBrowserSelectCallback file_browser_select_cb;
    int current_view;
    // Playlist editing scene state
    char** playlist_entries;
    size_t playlist_entry_count;
    size_t playlist_entry_capacity;
    bool playlist_modified;
    bool is_stopped;
    FuriTimer* popup_timer;
    struct VariableItemList* playlist_edit_list;
    ReturnScene return_scene; // Add this field
    // "[+] Add folder"
    FuriString* folder_path; // last .sub folder used (browser opens there next time)
    View* folder_view; // the browser screen
    FuriTimer* folder_scroll_timer; // scrolls long names
    FuriString* browse_path; // folder the browser is showing right now
    FuriString* pending_path; // file/folder a dialog is about (import, delete, rename)
    BrowserMode browser_mode;
    int browser_dialog; // which browser dialog is open
    bool return_to_browser; // Back in a dialog/keyboard goes back to the browser
} SubGhzPlaylistCreator;


typedef enum {
    SubGhzPlaylistCreatorViewSubmenu,
    SubGhzPlaylistCreatorViewPopup,
    SubGhzPlaylistCreatorViewTextInput,
    SubGhzPlaylistCreatorViewDialog,
    SubGhzPlaylistCreatorViewFileBrowser,
    SubGhzPlaylistCreatorViewPlaylistEdit,
    SubGhzPlaylistCreatorViewFolderPicker,
} SubGhzPlaylistCreatorView;

// Custom events
typedef enum {
    SubGhzPlaylistCreatorCustomEventShowMenu = 100, // Start custom events from 100 or higher
    SubGhzPlaylistCreatorCustomEventBackToPlaylist,
} SubGhzPlaylistCreatorCustomEvent;

// Function declarations
SubGhzPlaylistCreator* subghz_playlist_creator_alloc(void);
void subghz_playlist_creator_free(SubGhzPlaylistCreator* app);
bool subghz_playlist_creator_custom_callback(void* context, uint32_t custom_event);
bool subghz_playlist_creator_back_event_callback(void* context);
int32_t subghz_playlist_creator_app(void* p);
// Playlist editing scene
void scene_playlist_edit_show(SubGhzPlaylistCreator* app);
void subghz_playlist_creator_open_playlist(SubGhzPlaylistCreator* app, const char* path);
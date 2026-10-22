#include "../subghz_playlist_creator.h"
#include "scene_folder_picker.h"
#include "scene_dialog.h"
#include "scene_menu.h"
#include <furi.h>
#include <storage/storage.h>
#include <gui/view.h>
#include <gui/elements.h>
#include <gui/modules/popup.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/text_input.h>
#include <string.h>
#include <subghz_playlist_creator_icons.h> // icons from the images folder

#define TAG "Browser"

#define SUBGHZ_ROOT "/ext/subghz" // .sub browser starts here (and can't go higher)
#define PLAYLIST_ROOT "/ext/subghz/playlist" // playlist browser starts here
#define NAME_LEN 128

// ===========================================================================
// Small helpers
// ===========================================================================

// Compare two names, ignoring upper/lower case (so "b.sub" comes after "A.sub")
static int compare_names(const char* a, const char* b) {
    while(*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
        if(ca != cb) return (unsigned char)ca - (unsigned char)cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

// Sort a list of names alphabetically (same order you see in the Flipper file browser)
static void sort_names(char** names, size_t count) {
    for(size_t i = 1; i < count; i++) {
        char* key = names[i];
        size_t j = i;
        while(j > 0 && compare_names(names[j - 1], key) > 0) {
            names[j] = names[j - 1];
            j--;
        }
        names[j] = key;
    }
}

// Does the name end with ext (e.g. ".sub")? Upper/lower case doesn't matter.
static bool has_extension(const char* name, const char* ext) {
    size_t len = strlen(name);
    size_t ext_len = strlen(ext);
    if(len <= ext_len) return false;
    return compare_names(name + len - ext_len, ext) == 0;
}

// A simple growing list of text strings
typedef struct {
    char** items;
    size_t count;
    size_t capacity;
} NameList;

static void name_list_add(NameList* list, const char* text) {
    if(list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 16;
        list->items = realloc(list->items, list->capacity * sizeof(char*));
    }
    list->items[list->count++] = strdup(text);
}

static void name_list_clear(NameList* list) {
    for(size_t i = 0; i < list->count; i++) free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

// Looks into ONE folder and collects the files with the given extension and the
// subfolders (both sorted). Pass NULL for a list you don't need.
static void read_folder(
    Storage* storage,
    const char* path,
    const char* ext,
    NameList* files,
    NameList* folders) {
    File* dir = storage_file_alloc(storage);
    char name[NAME_LEN];
    FileInfo info;

    if(storage_dir_open(dir, path)) {
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            if(name[0] == '.') continue; // skip hidden stuff
            if(file_info_is_dir(&info)) {
                if(folders) name_list_add(folders, name);
            } else if(has_extension(name, ext)) {
                if(files) name_list_add(files, name);
            }
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);

    if(files) sort_names(files->items, files->count);
    if(folders) sort_names(folders->items, folders->count);
}

static bool folder_has_subfolders(Storage* storage, const char* path) {
    NameList folders = {0};
    read_folder(storage, path, ".sub", NULL, &folders);
    bool has = folders.count > 0;
    name_list_clear(&folders);
    return has;
}

// Adds one path to the playlist. Returns false if the playlist is full.
static bool add_to_playlist(SubGhzPlaylistCreator* app, const char* path) {
    if(app->playlist_entry_count >= MAX_PLAYLIST_ENTRIES) return false;
    if(app->playlist_entry_count == app->playlist_entry_capacity) {
        app->playlist_entry_capacity = app->playlist_entry_capacity ?
                                           app->playlist_entry_capacity * 2 :
                                           8;
        app->playlist_entries =
            realloc(app->playlist_entries, app->playlist_entry_capacity * sizeof(char*));
    }
    app->playlist_entries[app->playlist_entry_count++] = strdup(path);
    app->playlist_modified = true;
    return true;
}

static void clear_playlist(SubGhzPlaylistCreator* app) {
    for(size_t i = 0; i < app->playlist_entry_count; i++) free(app->playlist_entries[i]);
    free(app->playlist_entries);
    app->playlist_entries = NULL;
    app->playlist_entry_count = 0;
    app->playlist_entry_capacity = 0;
    app->playlist_modified = false;
}

// Goes through the folder and adds every .sub file.
// If include_subfolders is true it also goes into every folder inside it
// (first the files of a folder, then its subfolders one after another, A to Z).
// Returns how many files were added. *playlist_full is set if we hit the limit.
static size_t add_folder(
    SubGhzPlaylistCreator* app,
    const char* start_folder,
    bool include_subfolders,
    bool* playlist_full) {
    NameList todo = {0}; // folders we still have to look into
    NameList files = {0};
    NameList folders = {0};
    FuriString* path = furi_string_alloc();
    size_t added = 0;
    *playlist_full = false;

    name_list_add(&todo, start_folder);

    while(todo.count > 0 && !*playlist_full) {
        // take the last folder from the to-do list
        char* folder = todo.items[--todo.count];

        read_folder(app->storage, folder, ".sub", &files, include_subfolders ? &folders : NULL);

        for(size_t i = 0; i < files.count; i++) {
            furi_string_printf(path, "%s/%s", folder, files.items[i]);
            if(!add_to_playlist(app, furi_string_get_cstr(path))) {
                *playlist_full = true;
                break;
            }
            added++;
        }

        // Put the subfolders on the to-do list backwards,
        // so the alphabetically first one is handled next.
        for(size_t i = folders.count; i > 0; i--) {
            furi_string_printf(path, "%s/%s", folder, folders.items[i - 1]);
            name_list_add(&todo, furi_string_get_cstr(path));
        }

        name_list_clear(&files);
        name_list_clear(&folders);
        free(folder);
    }

    name_list_clear(&todo);
    furi_string_free(path);
    return added;
}

// Writes app->playlist_entries into app->playlist_path. Returns true if it worked.
static bool save_playlist(SubGhzPlaylistCreator* app) {
    storage_simply_mkdir(app->storage, PLAYLIST_ROOT);
    File* file = storage_file_alloc(app->storage);
    bool ok = false;
    if(storage_file_open(
           file, furi_string_get_cstr(app->playlist_path), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        const char* header = "# SubGhz Playlist\n";
        ok = storage_file_write(file, header, strlen(header)) == strlen(header);
        for(size_t i = 0; ok && i < app->playlist_entry_count; i++) {
            const char* entry = app->playlist_entries[i];
            ok = storage_file_write(file, "sub: ", 5) == 5 &&
                 storage_file_write(file, entry, strlen(entry)) == strlen(entry) &&
                 storage_file_write(file, "\n", 1) == 1;
        }
        storage_file_close(file);
    }
    storage_file_free(file);
    return ok;
}

// ===========================================================================
// The browser screen
// ===========================================================================
// Looks and works like the Flipper's own file browser (same rows, icons,
// highlight bar and scrollbar). It is used in three ways (app->browser_mode):
//
// BrowserModeCreate / BrowserModeAddToEdit  (pick .sub files)
//   4 rows + "< Cancel" and "Select >" buttons at the bottom.
//   Up/Down move, OK opens a folder, Back goes up, LEFT = Cancel,
//   RIGHT or long OK = Select (highlighted folder or file; on ". ." the
//   folder you are in).
//
// BrowserModePlaylists  (the "Edit" menu)
//   5 rows like the original. OK opens a playlist, long OK opens a small
//   menu with "Rename" and "Delete".

#define ROW_HEIGHT 12 // same as the original
#define Y_OFFSET 3 // same as the original
#define TEXT_WIDTH 110 // same as the original
#define SCROLL_INTERVAL 333 // long names scroll like in the original
#define SCROLL_DELAY 2
#define MAX_FILES_SHOWN 200

typedef enum {
    ItemUp, // ". ."
    ItemFolder,
    ItemFile,
    ItemMore, // "... 12 more" (not selectable)
} ItemType;

typedef struct {
    char* name;
    uint8_t type;
} BrowserItem;

typedef struct {
    BrowserItem* items;
    size_t count;
    int32_t selected; // highlighted row
    int32_t offset; // first row shown on screen
    uint32_t scroll_counter; // for scrolling long names
    uint8_t rows; // 4 with buttons, 5 without
    bool show_buttons;
    bool menu_open; // long-press menu (Rename / Delete)
    uint8_t menu_index;
} BrowserModel;

// What a dialog opened from the browser is about
typedef enum {
    BrowserDialogSubfolders, // "Include subfolders?"
    BrowserDialogSummary, // "12 files imported"  Cancel / OK
    BrowserDialogNothingFound, // "No .sub files found"  Cancel / Back
    BrowserDialogMessage, // just an info, "OK" goes back to the browser
} BrowserDialog;

static const char* menu_items[] = {"Rename", "Delete"};
#define MENU_COUNT 2

static bool is_sub_mode(SubGhzPlaylistCreator* app) {
    return app->browser_mode != BrowserModePlaylists;
}

static const char* browser_root(SubGhzPlaylistCreator* app) {
    return is_sub_mode(app) ? SUBGHZ_ROOT : PLAYLIST_ROOT;
}

static bool is_at_root(SubGhzPlaylistCreator* app) {
    return strcmp(furi_string_get_cstr(app->browse_path), browser_root(app)) == 0;
}

static void model_clear(BrowserModel* model) {
    for(size_t i = 0; i < model->count; i++) free(model->items[i].name);
    free(model->items);
    model->items = NULL;
    model->count = 0;
}

static void model_add(BrowserModel* model, const char* name, ItemType type) {
    model->items = realloc(model->items, (model->count + 1) * sizeof(BrowserItem));
    model->items[model->count].name = strdup(name);
    model->items[model->count].type = type;
    model->count++;
}

// Keep the highlighted row visible, with one row of look-ahead like the original
static void model_fix_offset(BrowserModel* model) {
    int32_t rows = model->rows;
    if((int32_t)model->count <= rows) {
        model->offset = 0;
        return;
    }
    if(model->selected - model->offset < 1) model->offset = model->selected - 1;
    if(model->selected - model->offset > rows - 2) model->offset = model->selected - (rows - 2);
    if(model->offset < 0) model->offset = 0;
    if(model->offset > (int32_t)model->count - rows) model->offset = model->count - rows;
}

// Reads app->browse_path and fills the list.
// highlight = name of an entry to put the cursor on, or NULL for the first one.
static void browser_load(SubGhzPlaylistCreator* app, const char* highlight) {
    NameList files = {0};
    NameList folders = {0};
    const char* ext = is_sub_mode(app) ? ".sub" : ".txt";
    read_folder(app->storage, furi_string_get_cstr(app->browse_path), ext, &files, &folders);

    with_view_model(
        app->folder_view,
        BrowserModel * model,
        {
            model_clear(model);
            model->show_buttons = is_sub_mode(app);
            model->rows = model->show_buttons ? 4 : 5;
            model->menu_open = false;

            if(!is_at_root(app)) model_add(model, ". .", ItemUp);
            for(size_t i = 0; i < folders.count; i++) {
                model_add(model, folders.items[i], ItemFolder);
            }
            for(size_t i = 0; i < files.count && i < MAX_FILES_SHOWN; i++) {
                model_add(model, files.items[i], ItemFile);
            }
            if(files.count > MAX_FILES_SHOWN) {
                char more[32];
                snprintf(
                    more, sizeof(more), "... %u more", (unsigned)(files.count - MAX_FILES_SHOWN));
                model_add(model, more, ItemMore);
            }

            model->selected = 0;
            if(highlight) {
                for(size_t i = 0; i < model->count; i++) {
                    if(model->items[i].type != ItemUp &&
                       strcmp(model->items[i].name, highlight) == 0) {
                        model->selected = i;
                        break;
                    }
                }
            }
            model->scroll_counter = 0;
            model_fix_offset(model);
        },
        true);

    name_list_clear(&files);
    name_list_clear(&folders);
}

// Shows the browser again (after a dialog, rename, ...)
static void browser_resume(SubGhzPlaylistCreator* app, const char* highlight) {
    app->return_to_browser = false;
    browser_load(app, highlight);
    furi_timer_start(app->folder_scroll_timer, SCROLL_INTERVAL);
    app->current_view = SubGhzPlaylistCreatorViewFolderPicker;
    view_dispatcher_switch_to_view(app->view_dispatcher, SubGhzPlaylistCreatorViewFolderPicker);
}

// Name and type of the highlighted entry
static ItemType browser_get_selected(SubGhzPlaylistCreator* app, FuriString* name) {
    ItemType type = ItemMore;
    with_view_model(
        app->folder_view,
        BrowserModel * model,
        {
            if(model->selected < (int32_t)model->count) {
                type = model->items[model->selected].type;
                furi_string_set(name, model->items[model->selected].name);
            }
        },
        false);
    return type;
}

static void browser_go_up(SubGhzPlaylistCreator* app) {
    if(is_at_root(app)) return;
    size_t slash = furi_string_search_rchar(app->browse_path, '/', 0);
    if(slash == FURI_STRING_FAILURE || slash == 0) return;
    // remember the folder we came from, so the cursor lands on it
    FuriString* came_from = furi_string_alloc_set(app->browse_path);
    furi_string_right(came_from, slash + 1);
    furi_string_left(app->browse_path, slash);
    browser_load(app, furi_string_get_cstr(came_from));
    furi_string_free(came_from);
}

static void browser_leave(SubGhzPlaylistCreator* app) {
    furi_timer_stop(app->folder_scroll_timer);
    app->return_to_browser = false;
}

static void browser_show_dialog(
    SubGhzPlaylistCreator* app,
    BrowserDialog kind,
    const char* header,
    const char* text,
    const char* left,
    const char* right);

// Cancel: Create -> back to the main menu without saving,
//         Add    -> back to the playlist,
//         Edit   -> back to the main menu
static void browser_cancel(SubGhzPlaylistCreator* app) {
    browser_leave(app);
    if(app->browser_mode == BrowserModeAddToEdit) {
        scene_playlist_edit_show(app);
    } else {
        if(app->browser_mode == BrowserModeCreate) clear_playlist(app);
        scene_menu_show(app);
    }
}

// --- After files were added ------------------------------------------------

// "x files added" popup is done -> back to the playlist (sent as event so it runs in the right place)
static void result_popup_callback(void* context) {
    SubGhzPlaylistCreator* app = context;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, SubGhzPlaylistCreatorCustomEventBackToPlaylist);
}

static void browser_import_done(SubGhzPlaylistCreator* app, size_t added, bool full) {
    // the browser opens here again next time
    furi_string_set(app->folder_path, app->browse_path);

    char text[64];
    if(full) {
        snprintf(
            text,
            sizeof(text),
            "%u file%s imported\n(limit of %u reached)",
            (unsigned)added,
            added == 1 ? "" : "s",
            (unsigned)MAX_PLAYLIST_ENTRIES);
    } else {
        snprintf(text, sizeof(text), "%u file%s imported", (unsigned)added, added == 1 ? "" : "s");
    }

    if(app->browser_mode == BrowserModeCreate) {
        if(app->playlist_entry_count == 0) {
            browser_show_dialog(
                app,
                BrowserDialogNothingFound,
                furi_string_get_cstr(app->playlist_name),
                "No .sub files found",
                "Cancel",
                "Back");
        } else {
            browser_show_dialog(
                app,
                BrowserDialogSummary,
                furi_string_get_cstr(app->playlist_name),
                text,
                "Cancel",
                "OK");
        }
        return;
    }

    // Adding to a playlist that is being edited: short popup, then back to it
    browser_leave(app);
    if(added == 0) snprintf(text, sizeof(text), "No .sub files found");
    app->current_view = SubGhzPlaylistCreatorViewPopup;
    popup_reset(app->popup);
    popup_set_header(app->popup, "Add", 64, 2, AlignCenter, AlignTop);
    popup_set_text(app->popup, text, 64, 36, AlignCenter, AlignCenter);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, result_popup_callback);
    popup_set_timeout(app->popup, 1500);
    popup_enable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubGhzPlaylistCreatorViewPopup);
}

static void browser_import_folder(SubGhzPlaylistCreator* app, bool with_subfolders) {
    bool full = false;
    const char* folder = furi_string_get_cstr(app->pending_path);
    size_t added = add_folder(app, folder, with_subfolders, &full);
    FURI_LOG_I(TAG, "Added %u files from %s", (unsigned)added, folder);
    browser_import_done(app, added, full);
}

// RIGHT / long OK in the .sub browser
static void browser_select(SubGhzPlaylistCreator* app) {
    FuriString* name = furi_string_alloc();
    ItemType type = browser_get_selected(app, name);

    if(type == ItemFile) {
        // a single .sub file
        furi_string_printf(
            app->pending_path,
            "%s/%s",
            furi_string_get_cstr(app->browse_path),
            furi_string_get_cstr(name));
        bool full = !add_to_playlist(app, furi_string_get_cstr(app->pending_path));
        browser_import_done(app, full ? 0 : 1, full);
    } else {
        // a folder: the highlighted one, or the one we're in
        furi_string_set(app->pending_path, app->browse_path);
        if(type == ItemFolder) {
            furi_string_cat_printf(app->pending_path, "/%s", furi_string_get_cstr(name));
        }
        if(folder_has_subfolders(app->storage, furi_string_get_cstr(app->pending_path))) {
            browser_show_dialog(
                app,
                BrowserDialogSubfolders,
                "Subfolders",
                "Also import the\nsubfolders?",
                "No",
                "Yes");
        } else {
            browser_import_folder(app, false);
        }
    }
    furi_string_free(name);
}

// --- Rename / Delete (playlist browser) -------------------------------------

static void rename_done_callback(void* context) {
    SubGhzPlaylistCreator* app = context;
    if(strlen(app->text_buffer) == 0) return; // empty name: stay in the keyboard

    // old: pending_path, new: same folder + new name + .txt
    FuriString* new_path = furi_string_alloc_set(app->pending_path);
    size_t slash = furi_string_search_rchar(new_path, '/', 0);
    if(slash != FURI_STRING_FAILURE) furi_string_left(new_path, slash);
    furi_string_cat_printf(new_path, "/%s.txt", app->text_buffer);

    FuriString* new_name = furi_string_alloc_printf("%s.txt", app->text_buffer);

    if(furi_string_cmp(new_path, app->pending_path) == 0) {
        browser_resume(app, furi_string_get_cstr(new_name)); // unchanged
    } else if(storage_file_exists(app->storage, furi_string_get_cstr(new_path))) {
        browser_show_dialog(
            app, BrowserDialogMessage, "Rename", "A playlist with this\nname exists", NULL, "OK");
    } else if(
        storage_common_rename(
            app->storage,
            furi_string_get_cstr(app->pending_path),
            furi_string_get_cstr(new_path)) == FSE_OK) {
        browser_resume(app, furi_string_get_cstr(new_name));
    } else {
        browser_show_dialog(app, BrowserDialogMessage, "Rename", "Could not rename", NULL, "OK");
    }

    furi_string_free(new_name);
    furi_string_free(new_path);
}

static void browser_menu_action(SubGhzPlaylistCreator* app, uint8_t action) {
    FuriString* name = furi_string_alloc();
    if(browser_get_selected(app, name) != ItemFile) {
        furi_string_free(name);
        return;
    }
    furi_string_printf(
        app->pending_path,
        "%s/%s",
        furi_string_get_cstr(app->browse_path),
        furi_string_get_cstr(name));

    if(action == 0) {
        // Rename: keyboard with the current name (without ".txt")
        furi_string_left(name, furi_string_size(name) - 4);
        strlcpy(app->text_buffer, furi_string_get_cstr(name), MAX_TEXT_LENGTH);
        browser_leave(app);
        app->return_to_browser = true;
        text_input_reset(app->text_input);
        text_input_set_header_text(app->text_input, "New name");
        text_input_set_result_callback(
            app->text_input, rename_done_callback, app, app->text_buffer, MAX_TEXT_LENGTH, true);
        app->current_view = SubGhzPlaylistCreatorViewTextInput;
        view_dispatcher_switch_to_view(app->view_dispatcher, SubGhzPlaylistCreatorViewTextInput);
    } else {
        // Delete: right away, no extra question. Keep the cursor where it was.
        int32_t old_index = 0;
        with_view_model(
            app->folder_view, BrowserModel * model, { old_index = model->selected; }, false);
        storage_simply_remove(app->storage, furi_string_get_cstr(app->pending_path));
        browser_load(app, NULL);
        with_view_model(
            app->folder_view,
            BrowserModel * model,
            {
                if(model->count > 0) {
                    model->selected = old_index < (int32_t)model->count ? old_index :
                                                                          (int32_t)model->count - 1;
                    model_fix_offset(model);
                }
            },
            true);
    }
    furi_string_free(name);
}

// --- Dialogs ----------------------------------------------------------------

static void browser_dialog_callback(DialogExResult result, void* context) {
    SubGhzPlaylistCreator* app = context;
    bool right = (result == DialogExResultRight);
    bool left = (result == DialogExResultLeft);
    if(!right && !left) return;

    switch(app->browser_dialog) {
    case BrowserDialogSubfolders:
        browser_import_folder(app, right); // Yes = with subfolders
        break;
    case BrowserDialogSummary:
        app->return_to_browser = false;
        if(right) {
            bool ok = save_playlist(app);
            clear_playlist(app);
            if(!ok) {
                browser_show_dialog(
                    app, BrowserDialogMessage, "Error", "Could not save\nthe playlist", NULL, "OK");
                app->return_to_browser = false; // after the error: main menu
                return;
            }
        } else {
            clear_playlist(app);
        }
        scene_menu_show(app);
        break;
    case BrowserDialogNothingFound:
        if(right) {
            browser_resume(app, NULL);
        } else {
            app->return_to_browser = false;
            clear_playlist(app);
            scene_menu_show(app);
        }
        break;
    case BrowserDialogMessage:
        if(app->return_to_browser) {
            browser_resume(app, NULL);
        } else {
            scene_menu_show(app);
        }
        break;
    }
}

static void browser_show_dialog(
    SubGhzPlaylistCreator* app,
    BrowserDialog kind,
    const char* header,
    const char* text,
    const char* left,
    const char* right) {
    browser_leave(app);
    app->browser_dialog = kind;
    app->return_to_browser = true;
    dialog_ex_reset(app->dialog);
    scene_dialog_show_custom(app, header, text, left, right, browser_dialog_callback, app);
}

// Back button while a browser dialog or the rename keyboard is open
void scene_folder_picker_dialog_back(SubGhzPlaylistCreator* app) {
    if(app->current_view == SubGhzPlaylistCreatorViewDialog) {
        if(app->browser_dialog == BrowserDialogSummary ||
           app->browser_dialog == BrowserDialogNothingFound) {
            // same as Cancel: don't save
            browser_dialog_callback(DialogExResultLeft, app);
            return;
        }
        if(app->browser_dialog == BrowserDialogMessage && !app->return_to_browser) {
            scene_menu_show(app);
            return;
        }
    }
    browser_resume(app, NULL);
}

// --- Drawing ----------------------------------------------------------------

static void browser_draw(Canvas* canvas, void* _model) {
    BrowserModel* model = _model;
    int32_t rows = model->rows;
    bool show_scrollbar = (int32_t)model->count > rows;
    FuriString* text = furi_string_alloc();

    canvas_set_font(canvas, FontSecondary);

    for(int32_t row = 0; row < rows && model->offset + row < (int32_t)model->count; row++) {
        int32_t i = model->offset + row;
        BrowserItem* item = &model->items[i];
        int32_t y = Y_OFFSET + row * ROW_HEIGHT;
        bool is_selected = (i == model->selected);
        size_t scroll = 0;

        if(is_selected) {
            // black highlight bar with rounded corners, exactly like the original
            int32_t right = show_scrollbar ? 121 : 126;
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, y, right + 1, ROW_HEIGHT);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_dot(canvas, 0, y);
            canvas_draw_dot(canvas, 1, y);
            canvas_draw_dot(canvas, 0, y + 1);
            canvas_draw_dot(canvas, 0, y + ROW_HEIGHT - 1);
            canvas_draw_dot(canvas, right, y);
            canvas_draw_dot(canvas, right, y + ROW_HEIGHT - 1);
            if(!model->menu_open) {
                scroll = model->scroll_counter < SCROLL_DELAY ?
                             0 :
                             model->scroll_counter - SCROLL_DELAY;
            }
        } else {
            canvas_set_color(canvas, ColorBlack);
        }

        const Icon* icon = item->type == ItemUp     ? &I_fb_back_10px :
                           item->type == ItemFolder ? &I_fb_dir_10px :
                                                      &I_fb_unknown_10px;
        canvas_draw_icon(canvas, 2, y + 1, icon);

        furi_string_set(text, item->name);
        elements_scrollable_text_line(
            canvas,
            15,
            y + 9,
            show_scrollbar ? TEXT_WIDTH - 6 : TEXT_WIDTH,
            text,
            scroll,
            !is_selected);
    }
    canvas_set_color(canvas, ColorBlack);

    if(show_scrollbar) {
        elements_scrollbar_pos(
            canvas, 126, Y_OFFSET, rows * ROW_HEIGHT, model->selected, model->count);
    }

    // nothing in here except ". ."?
    size_t real_items = model->count;
    if(real_items > 0 && model->items[0].type == ItemUp) real_items--;
    if(real_items == 0) {
        canvas_draw_str_aligned(
            canvas, 64, Y_OFFSET + (rows * ROW_HEIGHT) / 2, AlignCenter, AlignCenter, "<Empty>");
    }

    // the two buttons at the bottom
    if(model->show_buttons) {
        elements_button_left(canvas, "Cancel");
        elements_button_right(canvas, "Select");
    }

    // long-press menu: a small box with "Rename" / "Delete"
    if(model->menu_open) {
        const int32_t w = 56, h = 4 + MENU_COUNT * 12 + 2;
        const int32_t x = 128 - w - 6, y = 14;
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_rbox(canvas, x, y, w, h, 3);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rframe(canvas, x, y, w, h, 3);
        for(uint8_t m = 0; m < MENU_COUNT; m++) {
            int32_t my = y + 3 + m * 12;
            if(m == model->menu_index) {
                canvas_draw_rbox(canvas, x + 2, my, w - 4, 12, 2);
                canvas_set_color(canvas, ColorWhite);
            }
            canvas_draw_str(canvas, x + 8, my + 9, menu_items[m]);
            canvas_set_color(canvas, ColorBlack);
        }
    }

    furi_string_free(text);
}

// --- Buttons ----------------------------------------------------------------

static bool browser_menu_input(SubGhzPlaylistCreator* app, InputEvent* event) {
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return true;
    uint8_t action = 0xFF;
    with_view_model(
        app->folder_view,
        BrowserModel * model,
        {
            if(event->key == InputKeyUp) {
                model->menu_index = model->menu_index ? model->menu_index - 1 : MENU_COUNT - 1;
            } else if(event->key == InputKeyDown) {
                model->menu_index = (model->menu_index + 1) % MENU_COUNT;
            } else if(event->key == InputKeyBack || event->key == InputKeyLeft) {
                model->menu_open = false;
            } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
                model->menu_open = false;
                action = model->menu_index;
            }
        },
        true);
    if(action != 0xFF) browser_menu_action(app, action);
    return true;
}

static bool browser_input(InputEvent* event, void* context) {
    SubGhzPlaylistCreator* app = context;

    bool menu_open = false;
    with_view_model(
        app->folder_view, BrowserModel * model, { menu_open = model->menu_open; }, false);
    if(menu_open) return browser_menu_input(app, event);

    if(event->key == InputKeyUp || event->key == InputKeyDown) {
        if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
            with_view_model(
                app->folder_view,
                BrowserModel * model,
                {
                    if(model->count > 0) {
                        if(event->key == InputKeyUp) {
                            model->selected = model->selected > 0 ? model->selected - 1 :
                                                                    (int32_t)model->count - 1;
                        } else {
                            model->selected = model->selected + 1 < (int32_t)model->count ?
                                                  model->selected + 1 :
                                                  0;
                        }
                        model->scroll_counter = 0;
                        model_fix_offset(model);
                    }
                },
                true);
        }
        return true;
    }

    if(event->key == InputKeyOk) {
        FuriString* name = furi_string_alloc();
        ItemType type = browser_get_selected(app, name);
        if(event->type == InputTypeShort) {
            if(type == ItemUp) {
                browser_go_up(app);
            } else if(type == ItemFolder) {
                furi_string_cat_printf(app->browse_path, "/%s", furi_string_get_cstr(name));
                browser_load(app, NULL);
            } else if(type == ItemFile && is_sub_mode(app)) {
                // like the original browser: OK on a file picks that file
                browser_select(app);
            } else if(type == ItemFile) {
                // Edit: open the playlist
                furi_string_printf(
                    app->pending_path,
                    "%s/%s",
                    furi_string_get_cstr(app->browse_path),
                    furi_string_get_cstr(name));
                browser_leave(app);
                subghz_playlist_creator_open_playlist(app, furi_string_get_cstr(app->pending_path));
            }
        } else if(event->type == InputTypeLong) {
            if(is_sub_mode(app)) {
                browser_select(app);
            } else if(type == ItemFile) {
                with_view_model(
                    app->folder_view,
                    BrowserModel * model,
                    {
                        model->menu_open = true;
                        model->menu_index = 0;
                    },
                    true);
            }
        }
        furi_string_free(name);
        return true;
    }

    if(event->type != InputTypeShort) return true;

    if(event->key == InputKeyRight) {
        if(is_sub_mode(app)) browser_select(app);
    } else if(event->key == InputKeyLeft) {
        // .sub browser: Cancel button. Playlist browser: one folder up, like the original
        if(is_sub_mode(app)) {
            browser_cancel(app);
        } else {
            browser_go_up(app);
        }
    } else if(event->key == InputKeyBack) {
        if(is_at_root(app)) {
            browser_cancel(app);
        } else {
            browser_go_up(app);
        }
    }
    return true;
}

static void browser_scroll_timer_callback(void* context) {
    SubGhzPlaylistCreator* app = context;
    with_view_model(app->folder_view, BrowserModel * model, { model->scroll_counter++; }, true);
}

// ===========================================================================
// Called from the rest of the app
// ===========================================================================

void scene_folder_picker_alloc(SubGhzPlaylistCreator* app) {
    app->folder_view = view_alloc();
    view_allocate_model(app->folder_view, ViewModelTypeLocking, sizeof(BrowserModel));
    view_set_context(app->folder_view, app);
    view_set_draw_callback(app->folder_view, browser_draw);
    view_set_input_callback(app->folder_view, browser_input);
    app->folder_scroll_timer =
        furi_timer_alloc(browser_scroll_timer_callback, FuriTimerTypePeriodic, app);
    app->browse_path = furi_string_alloc();
    app->pending_path = furi_string_alloc();
}

void scene_folder_picker_free(SubGhzPlaylistCreator* app) {
    furi_timer_stop(app->folder_scroll_timer);
    furi_timer_free(app->folder_scroll_timer);
    with_view_model(app->folder_view, BrowserModel * model, { model_clear(model); }, false);
    view_free(app->folder_view);
    furi_string_free(app->browse_path);
    furi_string_free(app->pending_path);
}

void scene_folder_picker_show(SubGhzPlaylistCreator* app, BrowserMode mode) {
    app->browser_mode = mode;
    if(mode == BrowserModePlaylists) {
        storage_simply_mkdir(app->storage, PLAYLIST_ROOT);
        furi_string_set_str(app->browse_path, PLAYLIST_ROOT);
    } else {
        if(mode == BrowserModeCreate) clear_playlist(app);
        // Start where you were last time, otherwise in /ext/subghz
        const char* last = furi_string_get_cstr(app->folder_path);
        if(furi_string_size(app->folder_path) == 0 ||
           strncmp(last, SUBGHZ_ROOT, strlen(SUBGHZ_ROOT)) != 0 ||
           !storage_dir_exists(app->storage, last)) {
            furi_string_set_str(app->folder_path, SUBGHZ_ROOT);
        }
        furi_string_set(app->browse_path, app->folder_path);
    }
    browser_resume(app, NULL);
}

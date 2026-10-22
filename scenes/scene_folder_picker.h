#pragma once
#include "../subghz_playlist_creator.h"

// The browser screen. Looks like the Flipper's file browser.
//   BrowserModeCreate     - pick .sub files/folder for a new playlist,
//                           then "x files imported"  < Cancel / OK >
//   BrowserModeAddToEdit  - pick .sub files/folder to add to the open playlist
//   BrowserModePlaylists  - pick a playlist to edit (long OK = Rename / Delete)
void scene_folder_picker_alloc(SubGhzPlaylistCreator* app);
void scene_folder_picker_free(SubGhzPlaylistCreator* app);

void scene_folder_picker_show(SubGhzPlaylistCreator* app, BrowserMode mode);

// Back button while a dialog or the keyboard opened from the browser is shown
void scene_folder_picker_dialog_back(SubGhzPlaylistCreator* app);

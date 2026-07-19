#ifndef MUSIALIZER_PRESET_STORE_H_
#define MUSIALIZER_PRESET_STORE_H_

#include <stdbool.h>
#include <stddef.h>

#include "scene_settings.h"

// The shared tuning-preset library: one strict-JSON file in the per-user
// data directory, edited by the Tune inspector and durable across every
// track and project. Track-local presets inside .musi files remain valid
// project data and are copied (never moved) into this store on open.

typedef enum Preset_Store_Result {
    PRESET_STORE_OK = 0,
    PRESET_STORE_MISSING,
    PRESET_STORE_ERROR_ARGUMENT,
    PRESET_STORE_ERROR_PATH,
    PRESET_STORE_ERROR_READ,
    PRESET_STORE_ERROR_FORMAT,
    PRESET_STORE_ERROR_DIRECTORY,
    PRESET_STORE_ERROR_WRITE,
} Preset_Store_Result;

// Resolves the per-user store path without touching the filesystem.
// MUSIALIZER_PRESET_STORE overrides everything (tests, portable setups);
// otherwise %APPDATA%\Musializer\presets.json on Windows,
// $HOME/Library/Application Support/Musializer/presets.json on macOS, and
// $XDG_DATA_HOME/musializer/presets.json (falling back to
// $HOME/.local/share/musializer/presets.json) elsewhere.
bool preset_store_default_path(char *buffer, size_t capacity);

// The stable scene token used in the store file, taken from the scene's
// persisted setting keys ("settings.loom.weight" -> "loom") so the mapping
// cannot drift from the .musi contract.
bool preset_store_scene_token(size_t scene_index, char *buffer,
                              size_t capacity);
bool preset_store_scene_from_token(const char *token, size_t *scene_index);

// Loads the store into a validated library. A missing file is the normal
// first-run state: the library is empty and the result is
// PRESET_STORE_MISSING. Any other failure leaves the library empty and the
// file untouched; callers must keep the store read-only until the user
// resolves it rather than overwriting recoverable data.
Preset_Store_Result preset_store_load(const char *path,
                                      Scene_Settings_Preset_Library *library);

// Serializes the library and atomically replaces the store file, creating
// missing parent directories first.
Preset_Store_Result preset_store_save(
    const char *path, const Scene_Settings_Preset_Library *library);

// Copies presets from source that the destination does not already hold.
// Identity is (scene, exact setting values): an identical snapshot is
// skipped no matter its name, while a same-named preset with different
// values is imported. Imports receive fresh destination ids. Presets that
// do not fit a full scene are counted in skipped and left out.
bool preset_store_merge(Scene_Settings_Preset_Library *destination,
                        const Scene_Settings_Preset_Library *source,
                        size_t *imported, size_t *skipped);

const char *preset_store_result_string(Preset_Store_Result result);

#endif // MUSIALIZER_PRESET_STORE_H_

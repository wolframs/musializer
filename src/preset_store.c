#include "preset_store.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "project_io.h"

enum { PRESET_STORE_MAX_FILE_SIZE = 1024*1024 };

static bool path_join(char *buffer, size_t capacity, const char *base,
                      const char *suffix)
{
    if (base == NULL || base[0] == '\0') return false;
    int written = snprintf(buffer, capacity, "%s%s", base, suffix);
    return written > 0 && (size_t)written < capacity;
}

bool preset_store_default_path(char *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0) return false;
    const char *override = getenv("MUSIALIZER_PRESET_STORE");
    if (override != NULL && override[0] != '\0') {
        int written = snprintf(buffer, capacity, "%s", override);
        return written > 0 && (size_t)written < capacity;
    }
#ifdef _WIN32
    return path_join(buffer, capacity, getenv("APPDATA"),
                     "\\Musializer\\presets.json");
#elif defined(__APPLE__)
    return path_join(buffer, capacity, getenv("HOME"),
                     "/Library/Application Support/Musializer/presets.json");
#else
    const char *data_home = getenv("XDG_DATA_HOME");
    if (data_home != NULL && data_home[0] != '\0') {
        return path_join(buffer, capacity, data_home,
                         "/musializer/presets.json");
    }
    return path_join(buffer, capacity, getenv("HOME"),
                     "/.local/share/musializer/presets.json");
#endif
}

bool preset_store_scene_token(size_t scene_index, char *buffer,
                              size_t capacity)
{
    if (buffer == NULL || capacity == 0) return false;
    const Scene_Setting_Descriptor *descriptor =
        scene_settings_descriptor(scene_index, 0);
    if (descriptor == NULL) return false;
    const char *key = descriptor->key;
    const char *start = strchr(key, '.');
    if (start == NULL) return false;
    start += 1;
    const char *end = strchr(start, '.');
    if (end == NULL || end == start) return false;
    size_t length = (size_t)(end - start);
    if (length >= capacity) return false;
    memcpy(buffer, start, length);
    buffer[length] = '\0';
    return true;
}

bool preset_store_scene_from_token(const char *token, size_t *scene_index)
{
    if (token == NULL || scene_index == NULL) return false;
    for (size_t scene = 0; scene < SCENE_SETTINGS_SCENE_COUNT; ++scene) {
        char candidate[MUSI_PROJECT_TYPE_CAPACITY];
        if (!preset_store_scene_token(scene, candidate, sizeof(candidate))) {
            return false;
        }
        if (strcmp(candidate, token) == 0) {
            *scene_index = scene;
            return true;
        }
    }
    return false;
}

static bool library_from_document(Scene_Settings_Preset_Library *library,
                                  const Musi_Preset_Store_Document *store)
{
    scene_settings_preset_library_init(library);
    for (size_t i = 0; i < store->preset_count; ++i) {
        const Musi_Scene_Preset *source = &store->presets[i];
        size_t scene = 0;
        if (!preset_store_scene_from_token(source->scene_name, &scene) ||
            source->id == UINT64_MAX ||
            library->counts[scene] >= SCENE_SETTINGS_PRESETS_PER_SCENE) {
            return false;
        }
        Scene_Settings_Preset *destination =
            &library->items[scene][library->counts[scene]++];
        destination->id = source->id;
        snprintf(destination->name, sizeof(destination->name), "%s",
                 source->name);
        destination->snapshot.captured = true;
        destination->snapshot.count = source->setting_count;
        memcpy(destination->snapshot.values, source->settings,
               source->setting_count*sizeof(source->settings[0]));
        if (!scene_settings_snapshot_valid(scene, &destination->snapshot)) {
            return false;
        }
        if (source->id >= library->next_id) {
            library->next_id = source->id + 1;
        }
    }
    return scene_settings_preset_library_valid(library);
}

static bool document_from_library(
    Musi_Preset_Store_Document *store,
    const Scene_Settings_Preset_Library *library)
{
    musi_preset_store_document_init(store);
    store->next_id = library->next_id > 0 ? library->next_id : 1;
    for (size_t scene = 0; scene < SCENE_SETTINGS_SCENE_COUNT; ++scene) {
        for (size_t index = 0; index < library->counts[scene]; ++index) {
            const Scene_Settings_Preset *source =
                &library->items[scene][index];
            if (store->preset_count >= MUSI_PROJECT_MAX_SCENE_PRESETS) {
                return false;
            }
            Musi_Scene_Preset *destination =
                &store->presets[store->preset_count++];
            memset(destination, 0, sizeof(*destination));
            destination->id = source->id;
            if (!preset_store_scene_token(scene, destination->scene_name,
                                          sizeof(destination->scene_name))) {
                return false;
            }
            snprintf(destination->name, sizeof(destination->name), "%s",
                     source->name);
            destination->setting_count = source->snapshot.count;
            memcpy(destination->settings, source->snapshot.values,
                   source->snapshot.count*sizeof(destination->settings[0]));
        }
    }
    return true;
}

Preset_Store_Result preset_store_load(const char *path,
                                      Scene_Settings_Preset_Library *library)
{
    if (library == NULL) return PRESET_STORE_ERROR_ARGUMENT;
    scene_settings_preset_library_init(library);
    if (path == NULL || path[0] == '\0') return PRESET_STORE_ERROR_ARGUMENT;

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return errno == ENOENT ? PRESET_STORE_MISSING :
                                 PRESET_STORE_ERROR_READ;
    }
    char *data = malloc(PRESET_STORE_MAX_FILE_SIZE);
    if (data == NULL) {
        fclose(file);
        return PRESET_STORE_ERROR_READ;
    }
    size_t size = fread(data, 1, PRESET_STORE_MAX_FILE_SIZE, file);
    bool read_error = ferror(file) != 0;
    bool truncated = size >= PRESET_STORE_MAX_FILE_SIZE &&
                     fgetc(file) != EOF;
    fclose(file);
    if (read_error || truncated || size == 0) {
        free(data);
        return read_error ? PRESET_STORE_ERROR_READ :
                            PRESET_STORE_ERROR_FORMAT;
    }

    Musi_Preset_Store_Document *store = malloc(sizeof(*store));
    if (store == NULL) {
        free(data);
        return PRESET_STORE_ERROR_READ;
    }
    Musi_Project_Io_Result decoded =
        musi_preset_store_deserialize(store, data, size);
    free(data);
    bool converted = decoded == MUSI_PROJECT_IO_OK &&
                     library_from_document(library, store);
    free(store);
    if (!converted) {
        scene_settings_preset_library_init(library);
        return PRESET_STORE_ERROR_FORMAT;
    }
    return PRESET_STORE_OK;
}

static bool ensure_parent_directories(const char *path)
{
    char partial[1024];
    size_t length = strlen(path);
    if (length >= sizeof(partial)) return false;
    memcpy(partial, path, length + 1);
    for (size_t i = 1; i < length; ++i) {
        if (partial[i] != '/' && partial[i] != '\\') continue;
        char saved = partial[i];
        partial[i] = '\0';
#ifdef _WIN32
        if (partial[i - 1] != ':' && _mkdir(partial) != 0 &&
            errno != EEXIST) return false;
#else
        if (mkdir(partial, 0755) != 0 && errno != EEXIST) return false;
#endif
        partial[i] = saved;
    }
    return true;
}

Preset_Store_Result preset_store_save(
    const char *path, const Scene_Settings_Preset_Library *library)
{
    if (path == NULL || path[0] == '\0' || library == NULL ||
        !scene_settings_preset_library_valid(library)) {
        return PRESET_STORE_ERROR_ARGUMENT;
    }
    Musi_Preset_Store_Document *store = malloc(sizeof(*store));
    if (store == NULL) return PRESET_STORE_ERROR_WRITE;
    if (!document_from_library(store, library)) {
        free(store);
        return PRESET_STORE_ERROR_ARGUMENT;
    }
    size_t required = 0;
    Musi_Project_Io_Result measured =
        musi_preset_store_serialize(store, NULL, 0, &required);
    if (measured != MUSI_PROJECT_IO_ERROR_OUTPUT_TOO_SMALL) {
        free(store);
        return PRESET_STORE_ERROR_ARGUMENT;
    }
    char *encoded = malloc(required);
    if (encoded == NULL) {
        free(store);
        return PRESET_STORE_ERROR_WRITE;
    }
    Musi_Project_Io_Result serialized =
        musi_preset_store_serialize(store, encoded, required, &required);
    free(store);
    if (serialized != MUSI_PROJECT_IO_OK) {
        free(encoded);
        return PRESET_STORE_ERROR_ARGUMENT;
    }
    if (!ensure_parent_directories(path)) {
        free(encoded);
        return PRESET_STORE_ERROR_DIRECTORY;
    }
    Musi_Project_File_Result written =
        musi_project_atomic_write(path, encoded, required - 1);
    free(encoded);
    return written == MUSI_PROJECT_FILE_OK ? PRESET_STORE_OK :
                                             PRESET_STORE_ERROR_WRITE;
}

static bool snapshots_equal(const Scene_Settings_Snapshot *first,
                            const Scene_Settings_Snapshot *second)
{
    if (first->count != second->count) return false;
    return memcmp(first->values, second->values,
                  first->count*sizeof(first->values[0])) == 0;
}

bool preset_store_merge(Scene_Settings_Preset_Library *destination,
                        const Scene_Settings_Preset_Library *source,
                        size_t *imported, size_t *skipped)
{
    if (imported != NULL) *imported = 0;
    if (skipped != NULL) *skipped = 0;
    if (destination == NULL || source == NULL ||
        !scene_settings_preset_library_valid(destination) ||
        !scene_settings_preset_library_valid(source)) return false;
    for (size_t scene = 0; scene < SCENE_SETTINGS_SCENE_COUNT; ++scene) {
        for (size_t index = 0; index < source->counts[scene]; ++index) {
            const Scene_Settings_Preset *candidate =
                &source->items[scene][index];
            bool present = false;
            for (size_t at = 0; at < destination->counts[scene]; ++at) {
                if (snapshots_equal(&destination->items[scene][at].snapshot,
                                    &candidate->snapshot)) {
                    present = true;
                    break;
                }
            }
            if (present) continue;
            if (destination->counts[scene] >=
                SCENE_SETTINGS_PRESETS_PER_SCENE) {
                if (skipped != NULL) *skipped += 1;
                continue;
            }
            Scene_Settings_Preset *slot =
                &destination->items[scene][destination->counts[scene]++];
            *slot = *candidate;
            slot->id = destination->next_id;
            destination->next_id += 1;
            if (imported != NULL) *imported += 1;
        }
    }
    return true;
}

const char *preset_store_result_string(Preset_Store_Result result)
{
    switch (result) {
    case PRESET_STORE_OK: return "ok";
    case PRESET_STORE_MISSING: return "no preset store file yet";
    case PRESET_STORE_ERROR_ARGUMENT: return "invalid preset store request";
    case PRESET_STORE_ERROR_PATH: return "preset store path unavailable";
    case PRESET_STORE_ERROR_READ: return "preset store could not be read";
    case PRESET_STORE_ERROR_FORMAT:
        return "preset store contents were rejected";
    case PRESET_STORE_ERROR_DIRECTORY:
        return "preset store directory could not be created";
    case PRESET_STORE_ERROR_WRITE:
        return "preset store could not be written";
    }
    return "unknown preset store result";
}

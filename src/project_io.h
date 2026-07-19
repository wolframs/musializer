#ifndef MUSIALIZER_PROJECT_IO_H_
#define MUSIALIZER_PROJECT_IO_H_
#include <stddef.h>
#include "project.h"

#define MUSI_PROJECT_JSON_MAX_INPUT (4u*1024u*1024u)
typedef enum {
    MUSI_PROJECT_IO_OK = 0, MUSI_PROJECT_IO_ERROR_NULL, MUSI_PROJECT_IO_ERROR_INPUT_SIZE,
    MUSI_PROJECT_IO_ERROR_SYNTAX, MUSI_PROJECT_IO_ERROR_UNKNOWN_FIELD,
    MUSI_PROJECT_IO_ERROR_DUPLICATE_FIELD, MUSI_PROJECT_IO_ERROR_MISSING_FIELD,
    MUSI_PROJECT_IO_ERROR_STRING, MUSI_PROJECT_IO_ERROR_NUMBER,
    MUSI_PROJECT_IO_ERROR_CAPACITY, MUSI_PROJECT_IO_ERROR_SCHEMA,
    MUSI_PROJECT_IO_ERROR_VALIDATION, MUSI_PROJECT_IO_ERROR_OUTPUT_TOO_SMALL,
    MUSI_PROJECT_IO_ERROR_ALLOCATION
} Musi_Project_Io_Result;

// Asset references are interpreted relative to the project before the legacy
// launch-directory fallback is considered.  The distinct success values let
// callers surface that fallback instead of silently making a project depend on
// the process working directory.
typedef enum {
    MUSI_PROJECT_PATH_RESOLVED_ABSOLUTE = 0,
    MUSI_PROJECT_PATH_RESOLVED_PROJECT_RELATIVE,
    MUSI_PROJECT_PATH_RESOLVED_LEGACY_CWD,
    MUSI_PROJECT_PATH_ERROR_NULL,
    MUSI_PROJECT_PATH_ERROR_NOT_FOUND,
    MUSI_PROJECT_PATH_ERROR_TOO_LONG
} Musi_Project_Path_Result;

// Result of converting a canonical runtime asset path into the representation
// written to a project. Relative paths are emitted only when they can be
// resolved beside the destination project back to the same existing file.
typedef enum {
    MUSI_PROJECT_STORED_PATH_RELATIVE = 0,
    MUSI_PROJECT_STORED_PATH_ABSOLUTE,
    MUSI_PROJECT_STORED_PATH_ERROR_NULL,
    MUSI_PROJECT_STORED_PATH_ERROR_INVALID,
    MUSI_PROJECT_STORED_PATH_ERROR_TOO_LONG
} Musi_Project_Stored_Path_Result;

typedef enum {
    MUSI_PROJECT_FILE_OK = 0,
    MUSI_PROJECT_FILE_ERROR_NULL,
    MUSI_PROJECT_FILE_ERROR_PATH,
    MUSI_PROJECT_FILE_ERROR_OPEN,
    MUSI_PROJECT_FILE_ERROR_WRITE,
    MUSI_PROJECT_FILE_ERROR_PERMISSIONS,
    MUSI_PROJECT_FILE_ERROR_SYNC,
    MUSI_PROJECT_FILE_ERROR_CLOSE,
    MUSI_PROJECT_FILE_ERROR_PUBLISH,
    MUSI_PROJECT_FILE_ERROR_DURABILITY
} Musi_Project_File_Result;

typedef enum Musi_Project_Asset_Category {
    MUSI_PROJECT_ASSET_AUDIO = 0,
    MUSI_PROJECT_ASSET_IMAGE,
} Musi_Project_Asset_Category;

typedef enum Musi_Project_Bundle_Result {
    MUSI_PROJECT_BUNDLE_OK = 0,
    MUSI_PROJECT_BUNDLE_ERROR_ARGUMENT,
    MUSI_PROJECT_BUNDLE_ERROR_PATH,
    MUSI_PROJECT_BUNDLE_ERROR_DIRECTORY,
    MUSI_PROJECT_BUNDLE_ERROR_SOURCE,
    MUSI_PROJECT_BUNDLE_ERROR_COPY,
    MUSI_PROJECT_BUNDLE_ERROR_SYNC,
    MUSI_PROJECT_BUNDLE_ERROR_IDENTITY,
    MUSI_PROJECT_BUNDLE_ERROR_COLLISION,
    MUSI_PROJECT_BUNDLE_ERROR_PUBLISH,
} Musi_Project_Bundle_Result;

// The per-user shared preset store file ("musializer.presets/v1"): the same
// strict JSON discipline and Musi_Scene_Preset record shape as the project
// codec, reused for tuning presets that live with the user instead of one
// track. Both directions validate ids (nonzero, unique, below next_id),
// bounded UTF-8 names, stable scene names, and finite bounded settings.
typedef struct Musi_Preset_Store_Document {
    uint64_t next_id;
    size_t preset_count;
    Musi_Scene_Preset presets[MUSI_PROJECT_MAX_SCENE_PRESETS];
} Musi_Preset_Store_Document;

void musi_preset_store_document_init(Musi_Preset_Store_Document *store);
Musi_Project_Io_Result musi_preset_store_serialize(
    const Musi_Preset_Store_Document *store, char *output, size_t capacity,
    size_t *required_size);
Musi_Project_Io_Result musi_preset_store_deserialize(
    Musi_Preset_Store_Document *destination, const char *input,
    size_t input_size);

// required_size includes the trailing NUL. Output remains untouched on error.
Musi_Project_Io_Result musi_project_json_serialize(const Musi_Project *project,
                                                   char *output, size_t capacity,
                                                   size_t *required_size);
// Destination is replaced only after complete parsing and model validation.
// Readers accept the original v1 contract without quality/authored-workspace
// fields, defaulting to High quality and empty lanes. Canonical writers always
// emit the complete current v1 workspace, including embedded semantic events.
Musi_Project_Io_Result musi_project_json_deserialize(Musi_Project *destination,
                                                     const char *input, size_t input_size);
const char *musi_project_io_result_string(Musi_Project_Io_Result result);

Musi_Project_Path_Result musi_project_resolve_asset_path(
    const char *project_path, const char *asset_path,
    char *resolved, size_t capacity);
// Resolves only a normalized project-relative descendant. Absolute paths,
// traversal, legacy-CWD fallback, and symlink escapes are rejected.
Musi_Project_Path_Result musi_project_resolve_bundled_asset_path(
    const char *project_path, const char *asset_path,
    char *resolved, size_t capacity);
Musi_Project_Path_Result musi_project_canonicalize_existing_file(
    const char *path, char *resolved, size_t capacity);
// canonical_asset_path must be an absolute path to an existing regular file.
// A normalized, traversal-free project-relative path is preferred for assets
// in the destination project's directory tree. The canonical absolute path is
// the portable fallback. Output remains untouched on error.
Musi_Project_Stored_Path_Result musi_project_asset_path_for_storage(
    const char *project_path, const char *canonical_asset_path,
    char *stored, size_t capacity);
const char *musi_project_stored_path_result_string(
    Musi_Project_Stored_Path_Result result);
// True only when both paths identify the same existing regular file. This also
// catches hard-link/file-ID aliases that string comparison misses.
bool musi_project_existing_files_alias(const char *first, const char *second);
uint64_t musi_project_process_id(void);
bool musi_project_path_result_is_success(Musi_Project_Path_Result result);
const char *musi_project_path_result_string(Musi_Project_Path_Result result);

// Produces a same-directory transaction name. process_id and nonce are
// explicit so the collision policy can be tested deterministically.
bool musi_project_temporary_path(const char *destination,
                                 uint64_t process_id, uint64_t nonce,
                                 char *temporary, size_t capacity);

// Writes through an exclusively-created sibling, flushes it, and atomically
// replaces destination.  Only the transaction file owned by this call is ever
// removed on failure.  POSIX directory metadata is synced where supported;
// Windows uses MOVEFILE_WRITE_THROUGH.
Musi_Project_File_Result musi_project_atomic_write(
    const char *destination, const void *data, size_t size);
const char *musi_project_file_result_string(Musi_Project_File_Result result);

// Copies an immutable content-addressed asset beneath
// <project-stem>.assets/{audio,images}/ and returns both its portable stored
// path and runtime path. Existing content is reused only after SHA-256
// verification. The project itself must be published after all assets succeed.
Musi_Project_Bundle_Result musi_project_bundle_asset(
    const char *project_path, Musi_Project_Asset_Category category,
    const char *source_path, const char *expected_sha256,
    char *stored_path, size_t stored_capacity,
    char *runtime_path, size_t runtime_capacity);

// Reconstructs the content-addressed paths and accepts them only when
// source_path identifies that already-published object. This deliberately does
// no content read: callers may use it only for metadata autosaves after this
// process has successfully hash-verified and published the same asset. Explicit
// saves and first publication must use musi_project_bundle_asset().
Musi_Project_Bundle_Result musi_project_reference_published_asset(
    const char *project_path, Musi_Project_Asset_Category category,
    const char *source_path, const char *expected_sha256,
    char *stored_path, size_t stored_capacity,
    char *runtime_path, size_t runtime_capacity);
const char *musi_project_bundle_result_string(Musi_Project_Bundle_Result result);
#endif

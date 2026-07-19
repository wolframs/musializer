// setenv/unsetenv for the path-resolution test on POSIX hosts.
#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L
#endif

#include "preset_store.h"
#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

// Headless tests cannot include scene.h (raylib); index 0 is the spectrum
// scene and index 8 is Loom in the pinned Scene_Id ordering.
enum {
    TEST_SCENE_SPECTRUM = 0,
    TEST_SCENE_LOOM = 8,
};

#define TEST_STORE_DIRECTORY "./build/tests/preset-store"
#define TEST_STORE_PATH TEST_STORE_DIRECTORY "/presets.json"

#ifndef _WIN32
static void reset_store_file(void)
{
    (void)mkdir("./build", 0755);
    (void)mkdir("./build/tests", 0755);
    (void)mkdir(TEST_STORE_DIRECTORY, 0755);
    (void)remove(TEST_STORE_PATH);
}

static void write_store_file(const char *contents)
{
    reset_store_file();
    FILE *file = fopen(TEST_STORE_PATH, "wb");
    if (file == NULL) return;
    fwrite(contents, 1, strlen(contents), file);
    fclose(file);
}

// One valid single-preset document for the spectrum scene, with optional
// surgical overrides for the malformed-document cases.
static void build_store_json(char *buffer, size_t capacity,
                             const char *scene_token, const char *id_text,
                             const char *next_id_text, size_t setting_count)
{
    size_t used = (size_t)snprintf(buffer, capacity,
        "{\"schema_version\":\"musializer.presets/v1\",\"next_id\":%s,"
        "\"presets\":[{\"id\":%s,\"scene_name\":\"%s\",\"name\":\"Test\","
        "\"settings\":[", next_id_text, id_text, scene_token);
    for (size_t index = 0; index < setting_count; ++index) {
        const Scene_Setting_Descriptor *descriptor =
            scene_settings_descriptor(TEST_SCENE_SPECTRUM,
                                      index < scene_settings_count(
                                          TEST_SCENE_SPECTRUM) ? index : 0);
        used += (size_t)snprintf(buffer + used, capacity - used, "%s%.17g",
                                 index ? "," : "",
                                 (double)descriptor->default_value);
    }
    (void)snprintf(buffer + used, capacity - used, "]}]}");
}

TEST(preset_store_scene_tokens_round_trip)
{
    for (size_t scene = 0; scene < SCENE_SETTINGS_SCENE_COUNT; ++scene) {
        char token[65];
        REQUIRE_TRUE(preset_store_scene_token(scene, token, sizeof(token)));
        EXPECT_TRUE(token[0] != '\0');
        size_t resolved = SCENE_SETTINGS_SCENE_COUNT;
        REQUIRE_TRUE(preset_store_scene_from_token(token, &resolved));
        EXPECT_EQ_SIZE(resolved, scene);
    }
    size_t resolved = 0;
    EXPECT_FALSE(preset_store_scene_from_token("imaginary", &resolved));
    EXPECT_FALSE(preset_store_scene_from_token(NULL, &resolved));
}

#ifndef _WIN32
TEST(preset_store_path_prefers_override_then_xdg_then_home)
{
    char buffer[1024];

    REQUIRE_TRUE(setenv("MUSIALIZER_PRESET_STORE", "/tmp/override.json",
                        1) == 0);
    REQUIRE_TRUE(preset_store_default_path(buffer, sizeof(buffer)));
    EXPECT_TRUE(strcmp(buffer, "/tmp/override.json") == 0);
    REQUIRE_TRUE(unsetenv("MUSIALIZER_PRESET_STORE") == 0);

    REQUIRE_TRUE(setenv("XDG_DATA_HOME", "/data-home", 1) == 0);
    REQUIRE_TRUE(preset_store_default_path(buffer, sizeof(buffer)));
    EXPECT_TRUE(strcmp(buffer,
                       "/data-home/musializer/presets.json") == 0);
    REQUIRE_TRUE(unsetenv("XDG_DATA_HOME") == 0);

    const char *saved_home = getenv("HOME");
    REQUIRE_TRUE(setenv("HOME", "/user-home", 1) == 0);
    REQUIRE_TRUE(preset_store_default_path(buffer, sizeof(buffer)));
    EXPECT_TRUE(strcmp(buffer,
                       "/user-home/.local/share/musializer/presets.json") == 0);
    if (saved_home != NULL) REQUIRE_TRUE(setenv("HOME", saved_home, 1) == 0);
    else (void)unsetenv("HOME");

    EXPECT_FALSE(preset_store_default_path(buffer, 8));
    EXPECT_FALSE(preset_store_default_path(NULL, sizeof(buffer)));
}
#endif

TEST(preset_store_round_trips_and_saves_deterministically)
{
    reset_store_file();
    Scene_Settings settings;
    scene_settings_init(&settings);
    Scene_Settings_Preset_Library library;
    scene_settings_preset_library_init(&library);

    size_t selected = 0;
    REQUIRE_TRUE(scene_settings_preset_save(&library, TEST_SCENE_SPECTRUM,
                                            "Bright", &settings, &selected));
    const Scene_Setting_Descriptor *loom_setting =
        scene_settings_descriptor(TEST_SCENE_LOOM, 0);
    REQUIRE_TRUE(loom_setting != NULL);
    REQUIRE_TRUE(scene_settings_set(&settings, TEST_SCENE_LOOM, 0,
                                    loom_setting->maximum));
    REQUIRE_TRUE(scene_settings_preset_save(&library, TEST_SCENE_LOOM,
                                            "Heavy weave", &settings,
                                            &selected));

    REQUIRE_TRUE(preset_store_save(TEST_STORE_PATH, &library) ==
                 PRESET_STORE_OK);

    Scene_Settings_Preset_Library loaded;
    REQUIRE_TRUE(preset_store_load(TEST_STORE_PATH, &loaded) ==
                 PRESET_STORE_OK);
    EXPECT_EQ_SIZE(loaded.counts[TEST_SCENE_SPECTRUM], 1);
    EXPECT_EQ_SIZE(loaded.counts[TEST_SCENE_LOOM], 1);
    EXPECT_TRUE(strcmp(loaded.items[TEST_SCENE_SPECTRUM][0].name,
                       "Bright") == 0);
    EXPECT_TRUE(strcmp(loaded.items[TEST_SCENE_LOOM][0].name,
                       "Heavy weave") == 0);
    EXPECT_TRUE(loaded.items[TEST_SCENE_LOOM][0].snapshot.values[0] ==
                loom_setting->maximum);
    EXPECT_TRUE(loaded.next_id == library.next_id);
    EXPECT_EQ_SIZE(loaded.items[TEST_SCENE_SPECTRUM][0].snapshot.count,
                   scene_settings_count(TEST_SCENE_SPECTRUM));

    // Deterministic serialization: an immediate re-save writes identical
    // bytes, so external sync tooling never sees phantom changes.
    FILE *file = fopen(TEST_STORE_PATH, "rb");
    REQUIRE_TRUE(file != NULL);
    char first[8192];
    size_t first_size = fread(first, 1, sizeof(first), file);
    fclose(file);
    REQUIRE_TRUE(preset_store_save(TEST_STORE_PATH, &loaded) ==
                 PRESET_STORE_OK);
    file = fopen(TEST_STORE_PATH, "rb");
    REQUIRE_TRUE(file != NULL);
    char second[8192];
    size_t second_size = fread(second, 1, sizeof(second), file);
    fclose(file);
    EXPECT_EQ_SIZE(second_size, first_size);
    EXPECT_TRUE(memcmp(first, second, first_size) == 0);
}

TEST(preset_store_load_reports_missing_and_rejects_bad_documents)
{
    reset_store_file();
    Scene_Settings_Preset_Library library;
    EXPECT_TRUE(preset_store_load(TEST_STORE_PATH, &library) ==
                PRESET_STORE_MISSING);

    char valid[4096];
    char token[65];
    REQUIRE_TRUE(preset_store_scene_token(TEST_SCENE_SPECTRUM, token,
                                          sizeof(token)));
    size_t count = scene_settings_count(TEST_SCENE_SPECTRUM);

    build_store_json(valid, sizeof(valid), token, "1", "2", count);
    write_store_file(valid);
    REQUIRE_TRUE(preset_store_load(TEST_STORE_PATH, &library) ==
                 PRESET_STORE_OK);
    EXPECT_EQ_SIZE(library.counts[TEST_SCENE_SPECTRUM], 1);

    // Unknown scene token.
    build_store_json(valid, sizeof(valid), "imaginary", "1", "2", count);
    write_store_file(valid);
    EXPECT_TRUE(preset_store_load(TEST_STORE_PATH, &library) ==
                PRESET_STORE_ERROR_FORMAT);
    EXPECT_EQ_SIZE(library.counts[TEST_SCENE_SPECTRUM], 0);

    // Wrong setting count for the scene.
    build_store_json(valid, sizeof(valid), token, "1", "2", count + 1);
    write_store_file(valid);
    EXPECT_TRUE(preset_store_load(TEST_STORE_PATH, &library) ==
                PRESET_STORE_ERROR_FORMAT);

    // id zero, id at or above next_id, and a schema mismatch.
    build_store_json(valid, sizeof(valid), token, "0", "2", count);
    write_store_file(valid);
    EXPECT_TRUE(preset_store_load(TEST_STORE_PATH, &library) ==
                PRESET_STORE_ERROR_FORMAT);
    build_store_json(valid, sizeof(valid), token, "7", "2", count);
    write_store_file(valid);
    EXPECT_TRUE(preset_store_load(TEST_STORE_PATH, &library) ==
                PRESET_STORE_ERROR_FORMAT);
    build_store_json(valid, sizeof(valid), token, "1", "2", count);
    char *version = strstr(valid, "presets/v1");
    REQUIRE_TRUE(version != NULL);
    version[8] = '9';
    write_store_file(valid);
    EXPECT_TRUE(preset_store_load(TEST_STORE_PATH, &library) ==
                PRESET_STORE_ERROR_FORMAT);

    // Unknown and duplicate members, truncation, and trailing garbage.
    write_store_file("{\"schema_version\":\"musializer.presets/v1\","
                     "\"next_id\":1,\"presets\":[],\"extra\":1}");
    EXPECT_TRUE(preset_store_load(TEST_STORE_PATH, &library) ==
                PRESET_STORE_ERROR_FORMAT);
    write_store_file("{\"schema_version\":\"musializer.presets/v1\","
                     "\"next_id\":1,\"next_id\":1,\"presets\":[]}");
    EXPECT_TRUE(preset_store_load(TEST_STORE_PATH, &library) ==
                PRESET_STORE_ERROR_FORMAT);
    write_store_file("{\"schema_version\":\"musializer.presets/v1\"");
    EXPECT_TRUE(preset_store_load(TEST_STORE_PATH, &library) ==
                PRESET_STORE_ERROR_FORMAT);
    build_store_json(valid, sizeof(valid), token, "1", "2", count);
    size_t length = strlen(valid);
    REQUIRE_TRUE(length + 2 < sizeof(valid));
    valid[length] = '!';
    valid[length + 1] = '\0';
    write_store_file(valid);
    EXPECT_TRUE(preset_store_load(TEST_STORE_PATH, &library) ==
                PRESET_STORE_ERROR_FORMAT);
}

#endif // !_WIN32

TEST(preset_store_merge_deduplicates_by_scene_and_values)
{
    Scene_Settings settings;
    scene_settings_init(&settings);
    Scene_Settings_Preset_Library destination;
    scene_settings_preset_library_init(&destination);
    Scene_Settings_Preset_Library source;
    scene_settings_preset_library_init(&source);
    size_t selected = 0;

    REQUIRE_TRUE(scene_settings_preset_save(&destination,
                                            TEST_SCENE_SPECTRUM, "Mine",
                                            &settings, &selected));
    // Identical values under another name are already present.
    REQUIRE_TRUE(scene_settings_preset_save(&source, TEST_SCENE_SPECTRUM,
                                            "Theirs", &settings, &selected));
    // Different values are a genuinely new preset.
    const Scene_Setting_Descriptor *descriptor =
        scene_settings_descriptor(TEST_SCENE_SPECTRUM, 0);
    REQUIRE_TRUE(scene_settings_set(&settings, TEST_SCENE_SPECTRUM, 0,
                                    descriptor->maximum));
    REQUIRE_TRUE(scene_settings_preset_save(&source, TEST_SCENE_SPECTRUM,
                                            "Louder", &settings, &selected));

    size_t imported = 0;
    size_t skipped = 0;
    REQUIRE_TRUE(preset_store_merge(&destination, &source, &imported,
                                    &skipped));
    EXPECT_EQ_SIZE(imported, 1);
    EXPECT_EQ_SIZE(skipped, 0);
    EXPECT_EQ_SIZE(destination.counts[TEST_SCENE_SPECTRUM], 2);
    EXPECT_TRUE(strcmp(destination.items[TEST_SCENE_SPECTRUM][1].name,
                       "Louder") == 0);
    EXPECT_TRUE(scene_settings_preset_library_valid(&destination));

    // Re-merging the same source imports nothing new.
    REQUIRE_TRUE(preset_store_merge(&destination, &source, &imported,
                                    &skipped));
    EXPECT_EQ_SIZE(imported, 0);

    // A full scene skips the overflow instead of corrupting the library.
    Scene_Settings_Preset_Library full;
    scene_settings_preset_library_init(&full);
    for (size_t index = 0; index < SCENE_SETTINGS_PRESETS_PER_SCENE;
         ++index) {
        char name[32];
        snprintf(name, sizeof(name), "Slot %zu", index);
        float value = descriptor->minimum +
            (descriptor->maximum - descriptor->minimum)*
            (float)index/(float)SCENE_SETTINGS_PRESETS_PER_SCENE;
        REQUIRE_TRUE(scene_settings_set(&settings, TEST_SCENE_SPECTRUM, 0,
                                        value));
        REQUIRE_TRUE(scene_settings_preset_save(&full, TEST_SCENE_SPECTRUM,
                                                name, &settings, &selected));
    }
    // Slot values could coincidentally equal a source snapshot, so derive
    // the expectation with the merge's own identity rule.
    size_t expected_skipped = 0;
    for (size_t index = 0; index < source.counts[TEST_SCENE_SPECTRUM];
         ++index) {
        const Scene_Settings_Snapshot *candidate =
            &source.items[TEST_SCENE_SPECTRUM][index].snapshot;
        bool present = false;
        for (size_t at = 0; at < full.counts[TEST_SCENE_SPECTRUM]; ++at) {
            const Scene_Settings_Snapshot *held =
                &full.items[TEST_SCENE_SPECTRUM][at].snapshot;
            if (held->count == candidate->count &&
                memcmp(held->values, candidate->values,
                       candidate->count*sizeof(held->values[0])) == 0) {
                present = true;
                break;
            }
        }
        if (!present) expected_skipped += 1;
    }
    REQUIRE_TRUE(preset_store_merge(&full, &source, &imported, &skipped));
    EXPECT_EQ_SIZE(imported, 0);
    EXPECT_EQ_SIZE(skipped, expected_skipped);
    EXPECT_EQ_SIZE(full.counts[TEST_SCENE_SPECTRUM],
                   SCENE_SETTINGS_PRESETS_PER_SCENE);
}

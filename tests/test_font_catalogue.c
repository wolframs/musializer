#include "font_catalogue.h"
#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER FONT_CATALOGUE_SCHEMA_HEADER "\t3\n"

static const char *const SAMPLE =
    HEADER
    "Roboto\tSans Serif\tcyrillic,greek,latin,latin-ext,vietnamese\n"
    "Playfair Display\tSerif\tlatin,latin-ext\n"
    "Space Mono\tMonospace\tlatin\n";

static Font_Catalogue_Result parse(Font_Catalogue *catalogue, const char *text)
{
    return font_catalogue_parse(catalogue, text, strlen(text));
}

TEST(font_catalogue_parses_families_categories_and_the_scripts_we_can_render)
{
    Font_Catalogue *catalogue = calloc(1, sizeof(*catalogue));
    REQUIRE_TRUE(catalogue != NULL);
    REQUIRE_TRUE(parse(catalogue, SAMPLE) == FONT_CATALOGUE_OK);
    EXPECT_EQ_SIZE(catalogue->count, 3);

    EXPECT_TRUE(strcmp(catalogue->entries[0].family, "Roboto") == 0);
    EXPECT_TRUE(catalogue->entries[0].category == FONT_CATEGORY_SANS_SERIF);
    EXPECT_EQ_U64(catalogue->entries[0].scripts,
                  FONT_SCRIPT_LATIN | FONT_SCRIPT_LATIN_EXT | FONT_SCRIPT_GREEK |
                  FONT_SCRIPT_CYRILLIC | FONT_SCRIPT_VIETNAMESE);

    // The catalogue's own order is popularity order and must be preserved:
    // re-sorting it alphabetically would bury every face anyone wants.
    EXPECT_TRUE(strcmp(catalogue->entries[1].family, "Playfair Display") == 0);
    EXPECT_TRUE(catalogue->entries[1].category == FONT_CATEGORY_SERIF);
    EXPECT_EQ_U64(catalogue->entries[1].scripts,
                  FONT_SCRIPT_LATIN | FONT_SCRIPT_LATIN_EXT);
    EXPECT_TRUE(catalogue->entries[2].category == FONT_CATEGORY_MONOSPACE);
    EXPECT_EQ_U64(catalogue->entries[2].scripts, FONT_SCRIPT_LATIN);
    free(catalogue);
}

TEST(font_catalogue_tolerates_growth_it_does_not_understand)
{
    Font_Catalogue *catalogue = calloc(1, sizeof(*catalogue));
    REQUIRE_TRUE(catalogue != NULL);
    // A category or a script this build has never heard of must not make the
    // whole catalogue unreadable. The family is still perfectly usable.
    REQUIRE_TRUE(parse(catalogue,
        HEADER "Newface\tKinetic Variable\tlatin,tibetan,klingon\n") ==
        FONT_CATALOGUE_OK);
    EXPECT_EQ_SIZE(catalogue->count, 1);
    EXPECT_TRUE(catalogue->entries[0].category == FONT_CATEGORY_UNKNOWN);
    EXPECT_EQ_U64(catalogue->entries[0].scripts, FONT_SCRIPT_LATIN);
    EXPECT_TRUE(strcmp(font_category_name(FONT_CATEGORY_UNKNOWN), "Other") == 0);
    free(catalogue);
}

TEST(font_catalogue_rejects_a_file_it_did_not_expect_and_keeps_the_old_one)
{
    Font_Catalogue *catalogue = calloc(1, sizeof(*catalogue));
    REQUIRE_TRUE(catalogue != NULL);
    REQUIRE_TRUE(parse(catalogue, SAMPLE) == FONT_CATALOGUE_OK);

    static const struct { const char *text; Font_Catalogue_Result result; } bad[] = {
        { "", FONT_CATALOGUE_ERROR_HEADER },
        { "musializer.font-catalogue/v1", FONT_CATALOGUE_ERROR_HEADER },
        { "musializer.font-catalogue/v2\t1\nA\tSerif\tlatin\n",
          FONT_CATALOGUE_ERROR_HEADER },
        { "not a header at all\n", FONT_CATALOGUE_ERROR_HEADER },
        // Ragged rows: too few columns and too many.
        { HEADER "Roboto\tSans Serif\n", FONT_CATALOGUE_ERROR_ROW },
        { HEADER "Roboto\tSans Serif\tlatin\textra\n", FONT_CATALOGUE_ERROR_ROW },
        { HEADER "\tSans Serif\tlatin\n", FONT_CATALOGUE_ERROR_FAMILY },
        // A control character would be drawn into the interface and handed to
        // the helper as an argument.
        { HEADER "Rob\x01oto\tSans Serif\tlatin\n", FONT_CATALOGUE_ERROR_FAMILY },
        { HEADER, FONT_CATALOGUE_ERROR_EMPTY },
    };
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        Font_Catalogue_Result result = parse(catalogue, bad[i].text);
        EXPECT_TRUE(result == bad[i].result);
        // Whatever went wrong, the catalogue already loaded is still there. A
        // failed refresh must not empty a working picker.
        EXPECT_EQ_SIZE(catalogue->count, 3);
        EXPECT_TRUE(strcmp(catalogue->entries[0].family, "Roboto") == 0);
    }
    EXPECT_TRUE(font_catalogue_parse(NULL, SAMPLE, strlen(SAMPLE)) ==
                FONT_CATALOGUE_ERROR_ARGUMENT);
    EXPECT_TRUE(font_catalogue_parse(catalogue, NULL, 0) ==
                FONT_CATALOGUE_ERROR_ARGUMENT);
    free(catalogue);
}

TEST(font_catalogue_refuses_a_family_name_longer_than_it_can_hold)
{
    Font_Catalogue *catalogue = calloc(1, sizeof(*catalogue));
    REQUIRE_TRUE(catalogue != NULL);
    char row[FONT_CATALOGUE_FAMILY_CAPACITY + 64];
    // Exactly at capacity minus the terminator is the longest usable name.
    size_t longest = FONT_CATALOGUE_FAMILY_CAPACITY - 1;
    memset(row, 'A', longest);
    row[longest] = '\0';
    char text[sizeof(row) + 64];
    snprintf(text, sizeof(text), HEADER "%s\tSerif\tlatin\n", row);
    EXPECT_TRUE(parse(catalogue, text) == FONT_CATALOGUE_OK);
    EXPECT_EQ_SIZE(strlen(catalogue->entries[0].family), longest);

    memset(row, 'A', longest + 1);
    row[longest + 1] = '\0';
    snprintf(text, sizeof(text), HEADER "%s\tSerif\tlatin\n", row);
    EXPECT_TRUE(parse(catalogue, text) == FONT_CATALOGUE_ERROR_FAMILY);
    free(catalogue);
}

TEST(font_catalogue_accepts_a_final_row_without_a_trailing_newline)
{
    Font_Catalogue *catalogue = calloc(1, sizeof(*catalogue));
    REQUIRE_TRUE(catalogue != NULL);
    REQUIRE_TRUE(parse(catalogue,
        HEADER "Roboto\tSans Serif\tlatin\nInter\tSans Serif\tlatin") ==
        FONT_CATALOGUE_OK);
    EXPECT_EQ_SIZE(catalogue->count, 2);
    EXPECT_TRUE(strcmp(catalogue->entries[1].family, "Inter") == 0);

    // And blank rows in the middle are skipped rather than counted.
    REQUIRE_TRUE(parse(catalogue,
        HEADER "Roboto\tSans Serif\tlatin\n\n\nInter\tSans Serif\tlatin\n") ==
        FONT_CATALOGUE_OK);
    EXPECT_EQ_SIZE(catalogue->count, 2);
    free(catalogue);
}

TEST(font_catalogue_filter_folds_case_and_reports_more_than_it_writes)
{
    Font_Catalogue *catalogue = calloc(1, sizeof(*catalogue));
    REQUIRE_TRUE(catalogue != NULL);
    REQUIRE_TRUE(parse(catalogue, SAMPLE) == FONT_CATALOGUE_OK);

    size_t indices[8];
    size_t written = 0;
    EXPECT_EQ_SIZE(font_catalogue_filter(catalogue, "", 0, indices, 8, &written), 3);
    EXPECT_EQ_SIZE(written, 3);
    EXPECT_EQ_SIZE(font_catalogue_filter(catalogue, NULL, 0, indices, 8, &written), 3);

    EXPECT_EQ_SIZE(font_catalogue_filter(catalogue, "SPACE", 0, indices, 8, &written), 1);
    EXPECT_EQ_SIZE(indices[0], 2);
    EXPECT_EQ_SIZE(font_catalogue_filter(catalogue, "air dis", 0, indices, 8, &written), 1);
    EXPECT_EQ_SIZE(indices[0], 1);
    EXPECT_EQ_SIZE(font_catalogue_filter(catalogue, "zzz", 0, indices, 8, &written), 0);
    EXPECT_EQ_SIZE(written, 0);

    // Only Roboto carries Cyrillic, so requiring it must exclude the rest
    // rather than offer a face that would render the lyric as empty boxes.
    EXPECT_EQ_SIZE(font_catalogue_filter(catalogue, "", FONT_SCRIPT_CYRILLIC,
                                         indices, 8, &written), 1);
    EXPECT_EQ_SIZE(indices[0], 0);
    EXPECT_EQ_SIZE(font_catalogue_filter(
        catalogue, "", FONT_SCRIPT_CYRILLIC | FONT_SCRIPT_GREEK, indices, 8,
        &written), 1);

    // A caller with room for one still learns that three matched, which is
    // what lets the picker say "showing 1 of 3" instead of silently truncating.
    EXPECT_EQ_SIZE(font_catalogue_filter(catalogue, "", 0, indices, 1, &written), 3);
    EXPECT_EQ_SIZE(written, 1);
    EXPECT_EQ_SIZE(font_catalogue_filter(catalogue, "", 0, NULL, 0, &written), 3);
    EXPECT_EQ_SIZE(written, 0);
    free(catalogue);
}

TEST(font_catalogue_find_matches_a_family_exactly)
{
    Font_Catalogue *catalogue = calloc(1, sizeof(*catalogue));
    REQUIRE_TRUE(catalogue != NULL);
    REQUIRE_TRUE(parse(catalogue, SAMPLE) == FONT_CATALOGUE_OK);
    size_t index = 99;
    EXPECT_TRUE(font_catalogue_find(catalogue, "Space Mono", &index));
    EXPECT_EQ_SIZE(index, 2);
    // A saved project names one family. Resolving "space mono" or "Space" to a
    // different face would silently retypeset someone's captions.
    EXPECT_TRUE(!font_catalogue_find(catalogue, "space mono", &index));
    EXPECT_TRUE(!font_catalogue_find(catalogue, "Space", &index));
    EXPECT_TRUE(!font_catalogue_find(catalogue, "", &index));
    EXPECT_TRUE(!font_catalogue_find(catalogue, NULL, &index));
    EXPECT_TRUE(font_catalogue_find(catalogue, "Roboto", NULL));
    free(catalogue);
}

#define MANIFEST_HEADER FONT_IMPORT_MANIFEST_HEADER "\t1\n"
#define GOOD_DIGEST "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

TEST(font_import_manifest_reads_one_row_and_refuses_anything_else)
{
    Font_Import_Manifest manifest;
    memset(&manifest, 0, sizeof(manifest));
    static const char *const good =
        MANIFEST_HEADER
        "Space Mono\t/tmp/j/spacemono.ttf\t" GOOD_DIGEST
        "\t/tmp/j/spacemono.licence.txt\t" GOOD_DIGEST "\tOFL-1.1\n";
    REQUIRE_TRUE(font_import_manifest_parse(&manifest, good, strlen(good)) ==
                 FONT_CATALOGUE_OK);
    EXPECT_TRUE(strcmp(manifest.family, "Space Mono") == 0);
    EXPECT_TRUE(strcmp(manifest.font_path, "/tmp/j/spacemono.ttf") == 0);
    EXPECT_TRUE(strcmp(manifest.font_sha256, GOOD_DIGEST) == 0);
    EXPECT_TRUE(strcmp(manifest.licence_name, "OFL-1.1") == 0);

    static const char *const bad[] = {
        "",
        FONT_IMPORT_MANIFEST_HEADER,
        MANIFEST_HEADER,
        "musializer.font-import/v2\t1\nA\t/a\t" GOOD_DIGEST "\t/b\t" GOOD_DIGEST "\tOFL\n",
        // Too few columns, and too many.
        MANIFEST_HEADER "A\t/a\t" GOOD_DIGEST "\t/b\t" GOOD_DIGEST "\n",
        MANIFEST_HEADER "A\t/a\t" GOOD_DIGEST "\t/b\t" GOOD_DIGEST "\tOFL\textra\n",
        // A digest that is short, uppercase, or not hex at all. Accepting one
        // would mean comparing it against a real hash and always failing, with
        // the confusing message that the download did not match.
        MANIFEST_HEADER "A\t/a\tabc\t/b\t" GOOD_DIGEST "\tOFL\n",
        MANIFEST_HEADER "A\t/a\t" GOOD_DIGEST "\t/b\tZZZ" GOOD_DIGEST "\tOFL\n",
        // Empty family, path, or licence name.
        MANIFEST_HEADER "\t/a\t" GOOD_DIGEST "\t/b\t" GOOD_DIGEST "\tOFL\n",
        MANIFEST_HEADER "A\t\t" GOOD_DIGEST "\t/b\t" GOOD_DIGEST "\tOFL\n",
        MANIFEST_HEADER "A\t/a\t" GOOD_DIGEST "\t/b\t" GOOD_DIGEST "\t\n",
    };
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        Font_Import_Manifest attempted = manifest;
        EXPECT_TRUE(font_import_manifest_parse(&attempted, bad[i], strlen(bad[i])) !=
                    FONT_CATALOGUE_OK);
        // Whatever failed, the caller still holds the import it already had.
        EXPECT_TRUE(memcmp(&attempted, &manifest, sizeof(manifest)) == 0);
    }
    EXPECT_TRUE(font_import_manifest_parse(NULL, good, strlen(good)) ==
                FONT_CATALOGUE_ERROR_ARGUMENT);
    EXPECT_TRUE(font_import_manifest_parse(&manifest, NULL, 0) ==
                FONT_CATALOGUE_ERROR_ARGUMENT);
}

TEST(font_import_manifest_refuses_a_path_longer_than_it_can_hold)
{
    Font_Import_Manifest manifest;
    char *text = malloc(FONT_IMPORT_PATH_CAPACITY + 512);
    REQUIRE_TRUE(text != NULL);
    char *path = malloc(FONT_IMPORT_PATH_CAPACITY + 8);
    REQUIRE_TRUE(path != NULL);
    memset(path, 'p', FONT_IMPORT_PATH_CAPACITY);
    path[FONT_IMPORT_PATH_CAPACITY] = '\0';
    snprintf(text, FONT_IMPORT_PATH_CAPACITY + 512,
             MANIFEST_HEADER "A\t%s\t" GOOD_DIGEST "\t/b\t" GOOD_DIGEST "\tOFL\n",
             path);
    EXPECT_TRUE(font_import_manifest_parse(&manifest, text, strlen(text)) ==
                FONT_CATALOGUE_ERROR_ROW);
    free(path);
    free(text);
}

TEST(font_scripts_describe_names_coverage_and_never_overruns)
{
    char text[64];
    font_scripts_describe(FONT_SCRIPT_LATIN | FONT_SCRIPT_GREEK, text, sizeof(text));
    EXPECT_TRUE(strcmp(text, "Latin, Greek") == 0);
    font_scripts_describe(0, text, sizeof(text));
    EXPECT_TRUE(strcmp(text, "no known script") == 0);

    // A buffer too small keeps whole names rather than half of one.
    char tiny[8];
    font_scripts_describe(FONT_SCRIPT_LATIN | FONT_SCRIPT_CYRILLIC, tiny, sizeof(tiny));
    EXPECT_TRUE(strcmp(tiny, "Latin") == 0);
    // Room for nothing at all is an empty list, not the phrase that means the
    // face covers nothing.
    char single[2];
    font_scripts_describe(FONT_SCRIPT_LATIN, single, sizeof(single));
    EXPECT_TRUE(single[0] == '\0');
    font_scripts_describe(FONT_SCRIPT_LATIN, NULL, 0);
}

#include "sha256.h"
#include "test_support.h"

#include <string.h>

static void expect_digest(const void *data, size_t size, const char *expected)
{
    char hex[SHA256_HEX_SIZE];
    REQUIRE_TRUE(sha256_digest_hex(data, size, hex));
    EXPECT_TRUE(strcmp(hex, expected) == 0);
}

TEST(sha256_matches_standard_known_vectors)
{
    expect_digest(NULL, 0,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    expect_digest("abc", 3,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    static const char long_vector[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    expect_digest(long_vector, sizeof(long_vector) - 1,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(sha256_incremental_updates_match_one_shot)
{
    static const char message[] = "incremental hashing across awkward boundaries";
    Sha256 sha;
    uint8_t digest[SHA256_DIGEST_SIZE];
    char hex[SHA256_HEX_SIZE];
    sha256_init(&sha);
    for (size_t i = 0; i < sizeof(message) - 1; ++i) {
        REQUIRE_TRUE(sha256_update(&sha, message + i, 1));
    }
    REQUIRE_TRUE(sha256_final(&sha, digest));
    REQUIRE_TRUE(sha256_hex(digest, hex));

    char one_shot[SHA256_HEX_SIZE];
    REQUIRE_TRUE(sha256_digest_hex(message, sizeof(message) - 1, one_shot));
    EXPECT_TRUE(strcmp(hex, one_shot) == 0);
    EXPECT_FALSE(sha256_update(&sha, message, 1));
    EXPECT_FALSE(sha256_final(&sha, digest));
}

TEST(sha256_handles_the_million_a_vector)
{
    char block[1000];
    memset(block, 'a', sizeof(block));
    Sha256 sha;
    uint8_t digest[SHA256_DIGEST_SIZE];
    char hex[SHA256_HEX_SIZE];
    sha256_init(&sha);
    for (size_t i = 0; i < 1000; ++i) REQUIRE_TRUE(sha256_update(&sha, block, sizeof(block)));
    REQUIRE_TRUE(sha256_final(&sha, digest));
    REQUIRE_TRUE(sha256_hex(digest, hex));
    EXPECT_TRUE(strcmp(hex,
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
}

TEST(sha256_file_hashes_in_binary_mode_and_preserves_output_on_failure)
{
    char hex[SHA256_HEX_SIZE];
    REQUIRE_TRUE(sha256_file_hex("resources/logo/logo-256.png", hex));
    EXPECT_TRUE(strcmp(hex,
        "217412ab34f3b2b75e3665ab50ca7be23f556a54088fb776d27b937235c1a9fb") == 0);

    memset(hex, 'x', sizeof(hex));
    EXPECT_FALSE(sha256_file_hex("tests/fixtures/does-not-exist", hex));
    for (size_t i = 0; i < sizeof(hex); ++i) EXPECT_TRUE(hex[i] == 'x');
}

TEST(sha256_rejects_invalid_arguments_without_writing_outputs)
{
    uint8_t digest[SHA256_DIGEST_SIZE];
    memset(digest, 0xa5, sizeof(digest));
    EXPECT_FALSE(sha256_digest(NULL, 1, digest));
    for (size_t i = 0; i < sizeof(digest); ++i) EXPECT_TRUE(digest[i] == 0xa5);

    Sha256 sha;
    sha256_init(&sha);
    EXPECT_TRUE(sha256_update(&sha, NULL, 0));
    EXPECT_FALSE(sha256_update(NULL, NULL, 0));
    EXPECT_FALSE(sha256_final(&sha, NULL));

    sha.buffer_size = sizeof(sha.buffer);
    EXPECT_FALSE(sha256_update(&sha, NULL, 0));
    EXPECT_FALSE(sha256_final(&sha, digest));
}

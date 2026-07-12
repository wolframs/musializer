#include "sha256.h"

#include <stdio.h>
#include <string.h>

static const uint32_t sha256_round_constants[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2),
};

static uint32_t rotate_right(uint32_t value, unsigned int count)
{
    return (value >> count) | (value << (32U - count));
}

static uint32_t load_u32_be(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static void store_u32_be(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void sha256_compress(Sha256 *sha, const uint8_t block[64])
{
    uint32_t schedule[64];
    for (size_t i = 0; i < 16; ++i) schedule[i] = load_u32_be(block + i*4);
    for (size_t i = 16; i < 64; ++i) {
        uint32_t s0 = rotate_right(schedule[i - 15], 7) ^
                      rotate_right(schedule[i - 15], 18) ^
                      (schedule[i - 15] >> 3);
        uint32_t s1 = rotate_right(schedule[i - 2], 17) ^
                      rotate_right(schedule[i - 2], 19) ^
                      (schedule[i - 2] >> 10);
        schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
    }

    uint32_t a = sha->state[0];
    uint32_t b = sha->state[1];
    uint32_t c = sha->state[2];
    uint32_t d = sha->state[3];
    uint32_t e = sha->state[4];
    uint32_t f = sha->state[5];
    uint32_t g = sha->state[6];
    uint32_t h = sha->state[7];

    for (size_t i = 0; i < 64; ++i) {
        uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choice = (e & f) ^ (~e & g);
        uint32_t temp1 = h + sum1 + choice + sha256_round_constants[i] + schedule[i];
        uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    sha->state[0] += a;
    sha->state[1] += b;
    sha->state[2] += c;
    sha->state[3] += d;
    sha->state[4] += e;
    sha->state[5] += f;
    sha->state[6] += g;
    sha->state[7] += h;
}

void sha256_init(Sha256 *sha)
{
    if (sha == NULL) return;
    *sha = (Sha256) {
        .state = {
            UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85),
            UINT32_C(0x3c6ef372), UINT32_C(0xa54ff53a),
            UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
            UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19),
        },
    };
}

bool sha256_update(Sha256 *sha, const void *data, size_t size)
{
    if (sha == NULL || sha->finalized || sha->buffer_size >= sizeof(sha->buffer) ||
        (data == NULL && size != 0)) return false;
    if (size == 0) return true;
    const uint8_t *bytes = data;
    sha->total_size += (uint64_t)size;

    if (sha->buffer_size != 0) {
        size_t available = sizeof(sha->buffer) - sha->buffer_size;
        size_t take = size < available ? size : available;
        if (take != 0) memcpy(sha->buffer + sha->buffer_size, bytes, take);
        sha->buffer_size += take;
        bytes += take;
        size -= take;
        if (sha->buffer_size == sizeof(sha->buffer)) {
            sha256_compress(sha, sha->buffer);
            sha->buffer_size = 0;
        }
    }

    while (size >= sizeof(sha->buffer)) {
        sha256_compress(sha, bytes);
        bytes += sizeof(sha->buffer);
        size -= sizeof(sha->buffer);
    }
    if (size != 0) {
        memcpy(sha->buffer, bytes, size);
        sha->buffer_size = size;
    }
    return true;
}

bool sha256_final(Sha256 *sha, uint8_t digest[SHA256_DIGEST_SIZE])
{
    if (sha == NULL || digest == NULL || sha->finalized ||
        sha->buffer_size >= sizeof(sha->buffer)) return false;
    uint64_t bit_size = sha->total_size*UINT64_C(8);
    size_t used = sha->buffer_size;
    sha->buffer[used++] = 0x80;

    if (used > 56) {
        memset(sha->buffer + used, 0, sizeof(sha->buffer) - used);
        sha256_compress(sha, sha->buffer);
        used = 0;
    }
    memset(sha->buffer + used, 0, 56 - used);
    for (size_t i = 0; i < 8; ++i) {
        sha->buffer[63 - i] = (uint8_t)(bit_size >> (i*8));
    }
    sha256_compress(sha, sha->buffer);

    uint8_t result[SHA256_DIGEST_SIZE];
    for (size_t i = 0; i < 8; ++i) store_u32_be(result + i*4, sha->state[i]);
    memcpy(digest, result, sizeof(result));
    sha->buffer_size = 0;
    sha->finalized = true;
    return true;
}

bool sha256_digest(const void *data, size_t size,
                   uint8_t digest[SHA256_DIGEST_SIZE])
{
    if (digest == NULL || (data == NULL && size != 0)) return false;
    Sha256 sha;
    uint8_t result[SHA256_DIGEST_SIZE];
    sha256_init(&sha);
    if (!sha256_update(&sha, data, size) || !sha256_final(&sha, result)) return false;
    memcpy(digest, result, sizeof(result));
    return true;
}

bool sha256_hex(const uint8_t digest[SHA256_DIGEST_SIZE],
                char hex[SHA256_HEX_SIZE])
{
    static const char alphabet[] = "0123456789abcdef";
    if (digest == NULL || hex == NULL) return false;
    for (size_t i = 0; i < SHA256_DIGEST_SIZE; ++i) {
        hex[i*2] = alphabet[digest[i] >> 4];
        hex[i*2 + 1] = alphabet[digest[i] & 0x0f];
    }
    hex[SHA256_HEX_SIZE - 1] = '\0';
    return true;
}

bool sha256_digest_hex(const void *data, size_t size,
                       char hex[SHA256_HEX_SIZE])
{
    uint8_t digest[SHA256_DIGEST_SIZE];
    char result[SHA256_HEX_SIZE];
    if (hex == NULL || !sha256_digest(data, size, digest) ||
        !sha256_hex(digest, result)) return false;
    memcpy(hex, result, sizeof(result));
    return true;
}

bool sha256_file(const char *path, uint8_t digest[SHA256_DIGEST_SIZE])
{
    if (path == NULL || digest == NULL) return false;
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;

    bool ok = true;
    Sha256 sha;
    uint8_t chunk[64*1024];
    uint8_t result[SHA256_DIGEST_SIZE];
    sha256_init(&sha);
    for (;;) {
        size_t count = fread(chunk, 1, sizeof(chunk), file);
        if (count != 0 && !sha256_update(&sha, chunk, count)) {
            ok = false;
            break;
        }
        if (count != sizeof(chunk)) {
            if (ferror(file)) ok = false;
            break;
        }
    }
    if (fclose(file) != 0) ok = false;
    if (ok) ok = sha256_final(&sha, result);
    if (ok) memcpy(digest, result, sizeof(result));
    return ok;
}

bool sha256_file_hex(const char *path, char hex[SHA256_HEX_SIZE])
{
    uint8_t digest[SHA256_DIGEST_SIZE];
    char result[SHA256_HEX_SIZE];
    if (hex == NULL || !sha256_file(path, digest) ||
        !sha256_hex(digest, result)) return false;
    memcpy(hex, result, sizeof(result));
    return true;
}

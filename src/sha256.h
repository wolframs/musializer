#ifndef MUSIALIZER_SHA256_H_
#define MUSIALIZER_SHA256_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    SHA256_DIGEST_SIZE = 32,
    SHA256_HEX_SIZE = 65,
};

typedef struct {
    uint32_t state[8];
    uint64_t total_size;
    uint8_t buffer[64];
    size_t buffer_size;
    bool finalized;
} Sha256;

void sha256_init(Sha256 *sha);
bool sha256_update(Sha256 *sha, const void *data, size_t size);
bool sha256_final(Sha256 *sha, uint8_t digest[SHA256_DIGEST_SIZE]);

bool sha256_digest(const void *data, size_t size,
                   uint8_t digest[SHA256_DIGEST_SIZE]);
bool sha256_hex(const uint8_t digest[SHA256_DIGEST_SIZE],
                char hex[SHA256_HEX_SIZE]);
bool sha256_digest_hex(const void *data, size_t size,
                       char hex[SHA256_HEX_SIZE]);

// Reads in bounded chunks. Output is left untouched if opening, reading, or
// closing the file fails.
bool sha256_file(const char *path, uint8_t digest[SHA256_DIGEST_SIZE]);
bool sha256_file_hex(const char *path, char hex[SHA256_HEX_SIZE]);

#endif // MUSIALIZER_SHA256_H_

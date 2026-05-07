/* Public domain SHA-256, derived from Brad Conte's crypto-algorithms */

#ifndef GRABIT_VENDOR_SHA256_H
#define GRABIT_VENDOR_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_SIZE 32
#define SHA256_HEX_SIZE 65

struct sha256_ctx {
	uint8_t data[64];
	uint32_t datalen;
	uint64_t bitlen;
	uint32_t state[8];
};

void sha256_init(struct sha256_ctx *c);
void sha256_update(struct sha256_ctx *c, const uint8_t *data, size_t len);
void sha256_final(struct sha256_ctx *c, uint8_t *digest);

void sha256_to_hex(const uint8_t *digest, char *out_hex);

#endif

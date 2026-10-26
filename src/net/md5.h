/* Minimal MD5 (RFC 1321) for HTTP Digest authentication. */
#pragma once
#include <stdint.h>
#include <stddef.h>
void md5_hex(const char *msg, size_t len, char out[33]);   /* lowercase hex digest */

/* incremental: for files that arrive in pieces (FTP) */
typedef struct { uint32_t h[4]; uint64_t len; uint8_t buf[64]; size_t n; } md5_ctx_t;
void md5_init(md5_ctx_t *c);
void md5_update(md5_ctx_t *c, const void *data, size_t len);
void md5_final(md5_ctx_t *c, uint8_t out[16]);

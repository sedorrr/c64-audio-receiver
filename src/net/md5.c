/* Minimal MD5 (RFC 1321) for HTTP Digest authentication. */
#include <string.h>
#include "md5.h"

static const uint32_t K[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
static const uint8_t R[64] = { 7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22, 5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
                               4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23, 6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21 };

static void block(uint32_t h[4], const uint8_t *p) {
    uint32_t w[16], a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] | ((uint32_t)p[4*i+1] << 8) | ((uint32_t)p[4*i+2] << 16) | ((uint32_t)p[4*i+3] << 24);
    for (int i = 0; i < 64; i++) {
        uint32_t f; int g;
        if (i < 16)      { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d;          g = (3 * i + 5) & 15; }
        else             { f = c ^ (b | ~d);       g = (7 * i) & 15; }
        uint32_t t = d; d = c; c = b;
        uint32_t x = a + f + K[i] + w[g];
        b = b + ((x << R[i]) | (x >> (32 - R[i])));
        a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
}

void md5_init(md5_ctx_t *c) {
    c->h[0] = 0x67452301; c->h[1] = 0xefcdab89; c->h[2] = 0x98badcfe; c->h[3] = 0x10325476;
    c->len = 0; c->n = 0;
}
void md5_update(md5_ctx_t *c, const void *data, size_t len) {
    const uint8_t *p = data;
    c->len += len;
    while (len) {
        size_t k = 64 - c->n; if (k > len) k = len;
        memcpy(c->buf + c->n, p, k); c->n += k; p += k; len -= k;
        if (c->n == 64) { block(c->h, c->buf); c->n = 0; }
    }
}
void md5_final(md5_ctx_t *c, uint8_t out[16]) {
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80;
    md5_update(c, &pad, 1);
    uint8_t z = 0;
    while (c->n != 56) md5_update(c, &z, 1);
    uint8_t lb[8];
    for (int k = 0; k < 8; k++) lb[k] = (uint8_t)(bits >> (8 * k));
    md5_update(c, lb, 8);
    for (int k = 0; k < 16; k++) out[k] = (uint8_t)(c->h[k / 4] >> (8 * (k % 4)));
}

void md5_hex(const char *msg, size_t len, char out[33]) {
    md5_ctx_t c; uint8_t d[16];
    md5_init(&c); md5_update(&c, msg, len); md5_final(&c, d);
    static const char hx[] = "0123456789abcdef";
    for (int k = 0; k < 16; k++) { out[2*k] = hx[d[k] >> 4]; out[2*k+1] = hx[d[k] & 15]; }
    out[32] = 0;
}

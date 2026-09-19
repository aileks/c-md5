/*
 * MD5 processes messages in 512-bit (i.e. 64-byte, little-endian) blocks.
 *
 * Each block is interpreted as 16 32-bit words.
 * The compression function then performs 64 operations divided into four rounds.
 *
 * See:
 *   RFC 1321, §3.4: "Process Message in 16-Word Blocks"
 *   https://www.rfc-editor.org/rfc/rfc1321.html#section-3.4
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#define DEBUG(ctx, fmt, ...)                                                                       \
    do {                                                                                           \
        if ((ctx)->debug) {                                                                        \
            fprintf(stderr, "[%s():%d] ", __func__, __LINE__);                                     \
            fprintf(stderr, fmt, ##__VA_ARGS__);                                                   \
        }                                                                                          \
    } while (false)

#define MD5_BLOCK_SIZE 64  // bytes in one MD5 block (RFC 1321 §3.4)
#define MD5_PAD_START 0x80 // first byte of the padding sequence
#define MD5_LENGTH_BYTES 8 // size of the trailing 64-bit length field
#define MD5_PAD_TARGET (MD5_BLOCK_SIZE - MD5_LENGTH_BYTES)

#define READ_BUFFER_SIZE 4096

/*
 * Left-rotation amounts for each of the 64 MD5 operations.
 *
 * The 64 operations are divided into four rounds of 16 operations:
 *   Round 1: 7, 12, 17, 22
 *   Round 2: 5,  9, 14, 20
 *   Round 3: 4, 11, 16, 23
 *   Round 4: 6, 10, 15, 21
 *
 * The values are repeated four times within each round.
 *
 * RFC 1321 refers to these as the S11..S44 shift constants.
 *
 * See:
 *   RFC 1321, §3.4 and Appendix A
 *   https://www.rfc-editor.org/rfc/rfc1321.html#section-3.4
 *   https://www.rfc-editor.org/rfc/rfc1321.html#page-7
 */
static uint32_t const s[] = {
    7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 5,  9,  14, 20, 5,  9,
    14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
    4,  11, 16, 23, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21,
};

/*
 * Additive constants used by the 64 MD5 operations.
 *
 * K[i] is derived from the sine function:
 *     K[i] = floor(2^32 * abs(sin(i + 1)))
 * where the sine argument is measured in radians.
 *
 * These constants are intentionally fixed by the MD5 specification rather
 * than generated at runtime. RFC 1321 calls this table T[1..64].
 *
 * See:
 *   RFC 1321, §3.4
 *   https://www.rfc-editor.org/rfc/rfc1321.html#section-3.4
 */
static uint32_t const K[] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

typedef struct {
    uint64_t byte_count;
    bool debug;
    uint8_t block[MD5_BLOCK_SIZE];
    size_t block_idx;
    uint32_t A;
    uint32_t B;
    uint32_t C;
    uint32_t D;
} Context;

static void process_block(Context *ctx)
{
    DEBUG(ctx, "processing block\n");

    uint32_t M[16];

    // clang-format off
    // break block into 16 32-bit words M[i], 0 <= j <= 15
    for (size_t i = 0; i < 16; ++i) {
        size_t j = i * 4;
        // bitwise operators cause integer promotion -> cast to uint32_t 
        M[i] = (uint32_t)ctx->block[j]
             | (uint32_t)ctx->block[j + 1] << 8
             | (uint32_t)ctx->block[j + 2] << 16
             | (uint32_t)ctx->block[j + 3] << 24;
    }
    // clang-format on

    // initialize hash state for this block
    uint32_t A = ctx->A;
    uint32_t B = ctx->B;
    uint32_t C = ctx->C;
    uint32_t D = ctx->D;

    for (size_t i = 0; i < 64; ++i) {
        uint32_t F;
        size_t g;

        if (i < 16) {
            // round 1
            F = (B & C) | (~B & D);
            g = i;
        } else if (i < 32) {
            // round 2
            F = (D & B) | (~D & C);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            // round 3
            F = B ^ C ^ D;
            g = (3 * i + 5) % 16;
        } else {
            // round 4
            F = C ^ (B | ~D);
            g = (7 * i) % 16;
        }

        // add the round function, message word, and constant
        F = F + A + K[i] + M[g];

        // rotate the working state for the next op
        A = D;
        D = C;
        C = B;
        B += (F << s[i]) | (F >> (32 - s[i]));
    }

    // add the transformed state back into the running hash
    ctx->A += A;
    ctx->B += B;
    ctx->C += C;
    ctx->D += D;
}

static void process_byte(Context *ctx, uint8_t byte)
{
    DEBUG(ctx, "processing byte: %u\n", byte);

    ctx->block[ctx->block_idx++] = byte;

    // process each complete 64-byte block
    if (ctx->block_idx == MD5_BLOCK_SIZE) {
        process_block(ctx);
        ctx->block_idx = 0;
    }
}

static int process_input(Context *ctx, int file_desc)
{
    uint8_t buf[READ_BUFFER_SIZE];

    while (true) {
        // read the next chunk from input
        ssize_t n = read(file_desc, buf, sizeof buf);

        if (n == -1) {
            // `read` returned an error
            switch (errno) {
                // EINTR means `read` was interrupted by a signal before finishing
                // this isn't an abort-level error -> retry read
                case EINTR:
                    continue;
                default:
                    perror("read");
                    return -1;
            }
        } else if (n == 0) {
            // reached EOF
            break;
        }

        for (ssize_t i = 0; i < n; ++i) {
            uint8_t byte = buf[i];
            process_byte(ctx, byte);
            ctx->byte_count++;
        }
    }

    // pad the message with a 1 bit followed by zero bits until 56 bytes remain
    // the file size is appended after the fact, hence the stopping point 56 bytes
    process_byte(ctx, MD5_PAD_START);
    while (ctx->block_idx != MD5_PAD_TARGET) {
        process_byte(ctx, 0);
    }

    // split the 64-bit bit count into its low and high 32-bit words
    uint64_t bit_count = ctx->byte_count << 3;
    uint32_t low = (uint32_t)bit_count;
    uint32_t high = (uint32_t)(bit_count >> 32);

    // append the low 32 bits of the message length
    for (int i = 0; i < 4; ++i) {
        uint8_t byte = low >> (i * 8) & 0xff;
        DEBUG(ctx, "[low bits @ %d]: %u\n", i, byte);
        process_byte(ctx, byte);
    }

    // append the high 32 bits of the message length
    for (int i = 0; i < 4; ++i) {
        uint8_t byte = high >> (i * 8) & 0xff;
        DEBUG(ctx, "[high bits @ %d]: %u\n", i, byte);
        process_byte(ctx, byte);
    }

    // double-check padded block was processed completely
    if (ctx->block_idx != 0) {
        fprintf(stderr, "bad block index: %zu\n", ctx->block_idx);
        return -1;
    }

    return 0;
}

static void print_hash(Context *ctx)
{
    uint32_t hash[] = {
        ctx->A,
        ctx->B,
        ctx->C,
        ctx->D,
    };

    // write each 32-bit word as 4 little-endian bytes
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            // mask the byte before printing it
            printf("%02x", ((hash[i] >> (j * 8)) & 0xff));
        }
    }

    putchar('\n');
}

int main(int argc, char **argv)
{
    Context ctx = {
        .byte_count = 0,
        .debug = getenv("DEBUG") != NULL,
        .block = {0},
        .block_idx = 0,
        .A = 0x67452301,
        .B = 0xefcdab89,
        .C = 0x98badcfe,
        .D = 0x10325476,
    };

    int file_desc = STDIN_FILENO;

    // read file arg
    if (argc > 1) {
        char *fname = argv[1];

        DEBUG(&ctx, "opening file: %s\n", fname);

        file_desc = open(fname, O_RDONLY);
        if (file_desc == -1) {
            perror("open");
            return 1;
        }
    }

    if (process_input(&ctx, file_desc) == -1) {
        fprintf(stderr, "process_input failed\n");
        return 1;
    }

    close(file_desc);
    print_hash(&ctx);

    return 0;
}

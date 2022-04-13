#ifndef AES_CCM
#define AES_CCM

#include "aes_cbc_mac.h"
#include "aes_ctr.h"
#include <cstdio>
#include <cctype>
#include <span>

namespace creep {

inline void util_log_hex(const void *data, size_t len, const char *str)
{
    printf("[%08llx] \"%s\" - %lu bytes \n", (long long unsigned) data, str, len);

    if (!data || !len)
        return;

    const uint8_t *p = static_cast<const uint8_t*>(data);

    for (size_t i = 0; i < len; ++i) {

        if (!(i & 15))
            printf("[%08llx]  ", (long long unsigned) p + i);

        printf("%02x ", p[i]);
        
        if ((i & 7) == 7)
            putchar(' ');

        if ((i & 15) == 15) {
            
            printf("|");

            for (int j = 15; j >= 0; --j) {

                char c = p[i - j];

                if (isprint(c))
                    printf("%c", c);
                else
                    printf(".");
            }
            printf("|\n");
        }
    }

    int rem = len - ((len >> 4) << 4);

    if (rem) {

        printf("%*c |", (16 - rem) * 3 + ((~rem & 8) >> 3), ' ');

        for (int j = rem; j; --j) {

            char c = p[len - j];

            if (isprint(c))
                printf("%c", c);
            else
                printf(".");
        }

        for (int j = 0; j < 16 - rem; ++j)
            putchar('.');

        printf("|\n");
    }
}

template<size_t L>
inline void ccm_ctr(AES &ctx, uint8_t *a_0, const uint8_t *nonce, const uint8_t *in, uint8_t *out, size_t len)
{
    static_assert(L > 1 && L < 9, "invalid length field size");

    constexpr size_t N_LEN = 15 - L;    // Nonce length
    constexpr size_t L_IDX = N_LEN + 1; // Start of length field 

    a_0[0] = L - 1;
    memcpy(&a_0[1], nonce, N_LEN);
    memset(&a_0[L_IDX], 0, L);

    uint8_t buf[16];
    uint8_t a_i[16];
    memcpy(a_i, a_0, 16);

    auto end = out + len;
    auto idx = 0;
    
    while (out < end) {
        if ((idx &= 0xf) == 0) {
            ctr_inc_counter<L>(a_i);
            ctx.encrypt(a_i, buf);
        }
        *out++ = buf[idx++] ^ *in++;
    }

    memset(&a_0[L_IDX], 0, L);
    ctx.encrypt(a_0, a_0);
}

template<size_t L>
inline void ccm_auth(AES &ctx, uint8_t *block, const uint8_t *nonce, const uint8_t *in, size_t len, const uint8_t *aad, size_t aad_len, size_t tag_len)
{
    static_assert(L > 1 && L < 9, "invalid length field size");

    constexpr size_t N_LEN = 15 - L;    // Nonce length
    constexpr size_t L_IDX = N_LEN + 1; // Start of length field 

    block[0] =  (aad_len ? 0x40 : 0x00)     | 
                (((tag_len - 2) / 2) << 3)  |
                (L - 1);

    memcpy(&block[1], nonce, N_LEN);

    for (size_t 
            i = L_IDX, 
            j = L - 1; 
        i < 16; ++i) 
    {
        block[i] = len >> (8 * j--);
    }
    ctx.encrypt(block, block);

    if (aad_len) {
        size_t start;
        if (aad_len < 65536 - 256) {
            block[0] ^= aad_len >> 8;
            block[1] ^= aad_len;
            start = 2;
        } else if (aad_len < 4294967296) {
            block[0] ^= 0xff;
            block[1] ^= 0xfe;
            block[2] ^= aad_len >> 24;
            block[3] ^= aad_len >> 16;
            block[4] ^= aad_len >> 8;
            block[5] ^= aad_len;
            start = 6;
        } else {
            block[0] ^= 0xff;
            block[1] ^= 0xff;
            block[2] ^= aad_len >> 56;
            block[3] ^= aad_len >> 48;
            block[4] ^= aad_len >> 40;
            block[5] ^= aad_len >> 32;
            block[6] ^= aad_len >> 24;
            block[7] ^= aad_len >> 16;
            block[8] ^= aad_len >> 8;
            block[9] ^= aad_len;
            start = 10;
        }
        cbc_mac_padded(ctx, block, aad, aad_len, start);
    }

    if (len) 
        cbc_mac_padded(ctx, block, in, len, 0);
}

template<size_t L = 2>
inline bool ccm_encrypt(
    const uint8_t *key, 
    const uint8_t *nonce,
    const uint8_t *aad, size_t aad_len,
          uint8_t *tag, size_t tag_len,
    const uint8_t *in, size_t len,
    uint8_t *out)
{
    if (!key || !nonce || !in || !out || !len)
        return false;

    if (aad && !aad_len)
        return false;

    if (tag_len > 16 || 
        tag_len < 4  || 
        tag_len & 1)
        return false;

    AES ctx{key};
    uint8_t block[16];

    // ANCHOR: Authentication

    ccm_auth<L>(ctx, block, nonce, in, len, aad, aad_len, tag_len);

    memcpy(tag, block, tag_len);

    // ANCHOR: Encryption

    ccm_ctr<L>(ctx, block, nonce, in, out, len);

    for (size_t i = 0; i < tag_len; ++i)
        tag[i] ^= block[i];

    return true;
}

template<size_t L = 2>
inline bool ccm_decrypt(
    const uint8_t *key, 
    const uint8_t *nonce,
    const uint8_t *aad, size_t aad_len,
    const uint8_t *tag, size_t tag_len,
    const uint8_t *in, size_t len,
    uint8_t *out)
{
    if (!key || !nonce || !in || !out || !len)
        return false;

    if (aad && !aad_len)
        return false;

    if (tag_len > 16 || 
        tag_len < 4  || 
        tag_len & 1)
        return false;

    AES ctx{key};
    uint8_t block[16];
    uint8_t mac[16];

    // ANCHOR: Decryption

    ccm_ctr<L>(ctx, block, nonce, in, out, len);

    for (size_t i = 0; i < tag_len; ++i)
        mac[i] = block[i] ^ tag[i];

    // ANCHOR: Authentication

    ccm_auth<L>(ctx, block, nonce, in, len, aad, aad_len, tag_len);

    if (memcmp(mac, block, tag_len)) {
        memset(out, 0, len);
        return false;
    }
    return true;
}

}

#endif
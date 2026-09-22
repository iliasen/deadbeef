/*
 * Direct Stream Transfer (DST) decoder
 * Copyright (c) 2014 Peter Ross <pross@xvid.org>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * Standalone extraction of FFmpeg's libavcodec/dstdec.c (FFmpeg n7.1),
 * implementing the dst_decoder.h API of this plugin.
 *
 * Differences from the FFmpeg original:
 * - a minimal MSB-first bit reader is used instead of GetBitContext
 *   (zero-padded past the end of the packet, never reads out of bounds);
 * - the output is packed interleaved DSD, one byte per 8 samples per
 *   channel (MSB first), matching the reference libdstdec layout,
 *   instead of FFmpeg's 4-byte-strided buffer + DSD->PCM translation;
 * - the sample rate is fixed to 2822400 Hz (DSD64, as used on SACD).
 *
 * Compared to the reference DST decoder (libsacd/dst2) this
 * implementation is more tolerant: it does not abort on arithmetic
 * decoder flush mismatches and on several strict header validations,
 * which fixes frames that the reference decoder rejects.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "dst_decoder.h"

#define DST_MAX_CHANNELS 6
#define DST_MAX_ELEMENTS (2 * DST_MAX_CHANNELS)

/* DSD64: 588 * 64 samples per channel per frame = 4704 bytes per channel */
#define DST_SAMPLES_PER_FRAME 37632
#define DST_FRAME_BYTES_PER_CH (DST_SAMPLES_PER_FRAME / 8)

static const int8_t fsets_code_pred_coeff[3][3] = {
    {  -8 },
    { -16,  8 },
    {  -9, -5, 6 },
};

static const int8_t probs_code_pred_coeff[3][3] = {
    {  -8 },
    { -16,  8 },
    { -24, 24, -8 },
};

// ---------------------------------------------------------------------------
// Minimal MSB-first bit reader (semantically compatible with FFmpeg's
// default GetBitContext; out-of-range reads return zero bits)

typedef struct bitreader {
    const uint8_t *buf;
    size_t size;   // size in bytes
    size_t index;  // position in bits
} bitreader;

static inline unsigned int
br_bit (bitreader *br) {
    size_t byte = br->index >> 3;
    unsigned int b = byte < br->size ? br->buf[byte] : 0;
    unsigned int bit = (b >> (7 - (br->index & 7))) & 1;
    br->index++;
    return bit;
}

static inline unsigned int
br_bits (bitreader *br, int n) {
    unsigned int v = 0;
    while (n-- > 0) {
        v = (v << 1) | br_bit (br);
    }
    return v;
}

static inline int
br_sbits (bitreader *br, int n) {
    unsigned int v = br_bits (br, n);
    if (n > 0 && (v & (1u << (n - 1)))) {
        v |= ~0u << n;
    }
    return (int)v;
}

static inline size_t
br_left (bitreader *br) {
    size_t total = br->size * 8;
    return br->index < total ? total - br->index : 0;
}

// ---------------------------------------------------------------------------
// FFmpeg helpers reduced to their used semantics

static inline int
ff_av_log2 (unsigned int x) {
    return x ? 31 - __builtin_clz (x) : 0;
}

static inline int
ff_av_clip (int a, int amin, int amax) {
    return a < amin ? amin : (a > amax ? amax : a);
}

static inline uint64_t
rd64le (const void *p) {
    uint64_t v;
    memcpy (&v, p, 8);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap64 (v);
#endif
    return v;
}

static inline void
wr64le (void *p, uint64_t v) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap64 (v);
#endif
    memcpy (p, &v, 8);
}

// Exact port of the (universal) slow path of get_ur_golomb_jpegls()
static int
get_ur_golomb_jpegls (bitreader *gb, int k, int limit, int esc_len) {
    int i;
    for (i = 0; i < limit && br_bit (gb) == 0 && br_left (gb) > 0; i++)
        ;
    if (i < limit - 1) {
        return (int)br_bits (gb, k) + (i << k);
    }
    else if (i == limit - 1) {
        return (int)br_bits (gb, esc_len) + 1;
    }
    return -1;
}

// ---------------------------------------------------------------------------

typedef struct ArithCoder {
    unsigned int a;
    unsigned int c;
} ArithCoder;

typedef struct Table {
    unsigned int elements;
    unsigned int length[DST_MAX_ELEMENTS];
    int coeff[DST_MAX_ELEMENTS][128];
} Table;

struct dst_decoder_s {
    int channels;
    ArithCoder ac;
    Table fsets, probs;
    uint8_t status[DST_MAX_CHANNELS][16];
    int16_t filter[DST_MAX_ELEMENTS][16][256];
    uint8_t reverse[256];
    unsigned long long frame_count;
};

static inline int
get_sr_golomb_dst (bitreader *gb, unsigned int k) {
    int v = get_ur_golomb_jpegls (gb, k, (int)br_left (gb), 0);
    if (v && br_bit (gb)) {
        v = -v;
    }
    return v;
}

static int
read_map (bitreader *gb, Table *t, unsigned int map[DST_MAX_CHANNELS], int channels) {
    int ch;
    t->elements = 1;
    map[0] = 0;
    if (!br_bit (gb)) {
        for (ch = 1; ch < channels; ch++) {
            int bits = ff_av_log2 (t->elements) + 1;
            map[ch] = br_bits (gb, bits);
            if (map[ch] == t->elements) {
                t->elements++;
                if (t->elements >= DST_MAX_ELEMENTS) {
                    return -1;
                }
            }
            else if (map[ch] > t->elements) {
                return -1;
            }
        }
    }
    else {
        memset (map, 0, sizeof (*map) * DST_MAX_CHANNELS);
    }
    return 0;
}

static void
read_uncoded_coeff (bitreader *gb, int *dst, unsigned int elements,
                    int coeff_bits, int is_signed, int offset) {
    unsigned int i;
    for (i = 0; i < elements; i++) {
        dst[i] = (is_signed ? br_sbits (gb, coeff_bits) : (int)br_bits (gb, coeff_bits)) + offset;
    }
}

static int
read_table (bitreader *gb, Table *t, const int8_t code_pred_coeff[3][3],
            int length_bits, int coeff_bits, int is_signed, int offset) {
    unsigned int i, j, k;
    for (i = 0; i < t->elements; i++) {
        t->length[i] = br_bits (gb, length_bits) + 1;
        if (!br_bit (gb)) {
            read_uncoded_coeff (gb, t->coeff[i], t->length[i], coeff_bits, is_signed, offset);
        }
        else {
            int method = br_bits (gb, 2), lsb_size;
            if (method == 3) {
                return -1;
            }

            read_uncoded_coeff (gb, t->coeff[i], method + 1, coeff_bits, is_signed, offset);

            lsb_size = br_bits (gb, 3);
            for (j = method + 1; j < t->length[i]; j++) {
                int c, x = 0;
                for (k = 0; k < (unsigned int)(method + 1); k++) {
                    x += code_pred_coeff[method][k] * (unsigned)t->coeff[i][j - k - 1];
                }
                c = get_sr_golomb_dst (gb, lsb_size);
                if (x >= 0) {
                    c -= (x + 4) / 8;
                }
                else {
                    c += (-x + 3) / 8;
                }
                if (!is_signed) {
                    if (c < offset || c >= offset + (1 << coeff_bits)) {
                        return -1;
                    }
                }
                t->coeff[i][j] = c;
            }
        }
    }
    return 0;
}

static void
ac_init (ArithCoder *ac, bitreader *gb) {
    ac->a = 4095;
    ac->c = br_bits (gb, 12);
}

static inline void
ac_get (ArithCoder *ac, bitreader *gb, int p, int *e) {
    unsigned int k = (ac->a >> 8) | ((ac->a >> 7) & 1);
    unsigned int q = k * p;
    unsigned int a_q = ac->a - q;

    *e = ac->c < a_q;
    if (*e) {
        ac->a = a_q;
    }
    else {
        ac->a = q;
        ac->c -= a_q;
    }

    if (ac->a < 2048) {
        int n = 11 - ff_av_log2 (ac->a);
        ac->a <<= n;
        ac->c = (ac->c << n) | br_bits (gb, n);
    }
}

static inline uint8_t
prob_dst_x_bit (dst_decoder_t *dec, int c) {
    return (dec->reverse[c & 127] >> 1) + 1;
}

static int
build_filter (int16_t table[DST_MAX_ELEMENTS][16][256], const Table *fsets) {
    int i, j, k, l;

    for (i = 0; i < (int)fsets->elements; i++) {
        int length = fsets->length[i];

        for (j = 0; j < 16; j++) {
            int total = ff_av_clip (length - j * 8, 0, 8);

            for (k = 0; k < 256; k++) {
                int64_t v = 0;

                for (l = 0; l < total; l++) {
                    v += (((k >> l) & 1) * 2 - 1) * fsets->coeff[i][j * 8 + l];
                }
                if ((int16_t)v != v) {
                    return -1;
                }
                table[i][j][k] = v;
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------

dst_decoder_t *
dst_decoder_create (int channel_count) {
    if (channel_count <= 0 || channel_count > DST_MAX_CHANNELS) {
        return NULL;
    }
    dst_decoder_t *dec = calloc (1, sizeof (*dec));
    if (!dec) {
        return NULL;
    }
    dec->channels = channel_count;
    for (int i = 0; i < 256; i++) {
        uint8_t v = (uint8_t)i, r = 0;
        for (int b = 0; b < 8; b++) {
            r = (uint8_t)((r << 1) | (v & 1));
            v >>= 1;
        }
        dec->reverse[i] = r;
    }
    dec->frame_count = 0;
    return dec;
}

void
dst_decoder_destroy (dst_decoder_t *dec) {
    free (dec);
}

int
dst_decoder_decode (dst_decoder_t *dec, const uint8_t *dst_data, size_t dst_size, uint8_t *dsd_out, size_t *dsd_out_size) {
    unsigned int map_ch_to_felem[DST_MAX_CHANNELS];
    unsigned int map_ch_to_pelem[DST_MAX_CHANNELS];
    unsigned int i, ch, same_map;
    int dst_x_bit;
    unsigned int half_prob[DST_MAX_CHANNELS];
    const int channels = dec->channels;
    const size_t frame_bytes = (size_t)DST_FRAME_BYTES_PER_CH * channels;
    ArithCoder *ac = &dec->ac;
    bitreader gb_s, *gb = &gb_s;
    int ret = -1;

    if (!dec || !dst_data || !dsd_out || !dsd_out_size) {
        return -1;
    }
    // Always report a full output frame so the caller never loses a frame;
    // on error the caller substitutes DSD silence.
    if (*dsd_out_size < frame_bytes) {
        return -1;
    }
    *dsd_out_size = frame_bytes;
    unsigned long long frame_no = dec->frame_count++;

    if (dst_size <= 1) {
        goto fail;
    }

    gb->buf = dst_data;
    gb->size = dst_size;
    gb->index = 0;

    if (!br_bit (gb)) {
        // uncompressed DSD frame
        br_bit (gb); // DST_X bit
        if (br_bits (gb, 6)) {
            goto fail;
        }
        memset (dsd_out, 0x55, frame_bytes);
        memcpy (dsd_out, dst_data + 1, dst_size - 1 < frame_bytes ? dst_size - 1 : frame_bytes);
        return 0;
    }

    /* Segmentation (10.4, 10.5, 10.6) */

    if (!br_bit (gb)) {
        goto fail; // "Not Same Segmentation" unsupported
    }
    if (!br_bit (gb)) {
        goto fail; // "Not Same Segmentation For All Channels" unsupported
    }
    if (!br_bit (gb)) {
        goto fail; // "Not End Of Channel Segmentation" unsupported
    }

    /* Mapping (10.7, 10.8, 10.9) */

    same_map = br_bit (gb);

    if (read_map (gb, &dec->fsets, map_ch_to_felem, channels) < 0) {
        goto fail;
    }

    if (same_map) {
        dec->probs.elements = dec->fsets.elements;
        memcpy (map_ch_to_pelem, map_ch_to_felem, sizeof (map_ch_to_felem));
    }
    else {
        if (read_map (gb, &dec->probs, map_ch_to_pelem, channels) < 0) {
            goto fail;
        }
    }

    /* Half Probability (10.10) */

    for (ch = 0; ch < (unsigned int)channels; ch++) {
        half_prob[ch] = br_bit (gb);
    }

    /* Filter Coef Sets (10.12) */

    if (read_table (gb, &dec->fsets, fsets_code_pred_coeff, 7, 9, 1, 0) < 0) {
        goto fail;
    }

    /* Probability Tables (10.13) */

    if (read_table (gb, &dec->probs, probs_code_pred_coeff, 6, 7, 0, 1) < 0) {
        goto fail;
    }

    /* Arithmetic Coded Data (10.11) */

    if (br_bit (gb)) {
        goto fail;
    }
    ac_init (ac, gb);

    if (build_filter (dec->filter, &dec->fsets) < 0) {
        goto fail;
    }

    memset (dec->status, 0xAA, sizeof (dec->status));
    memset (dsd_out, 0, frame_bytes);

    ac_get (ac, gb, prob_dst_x_bit (dec, dec->fsets.coeff[0][0]), &dst_x_bit);

    for (i = 0; i < DST_SAMPLES_PER_FRAME; i++) {
        for (ch = 0; ch < (unsigned int)channels; ch++) {
            const unsigned int felem = map_ch_to_felem[ch];
            int16_t (*filter)[256] = dec->filter[felem];
            uint8_t *status = dec->status[ch];
            int prob, residual, v;

#define F(x) filter[(x)][status[(x)]]
            const int16_t predict = F( 0) + F( 1) + F( 2) + F( 3) +
                                    F( 4) + F( 5) + F( 6) + F( 7) +
                                    F( 8) + F( 9) + F(10) + F(11) +
                                    F(12) + F(13) + F(14) + F(15);
#undef F

            if (!half_prob[ch] || i >= dec->fsets.length[felem]) {
                unsigned int pelem = map_ch_to_pelem[ch];
                unsigned int index = (unsigned int)(predict < 0 ? -predict : predict) >> 3;
                prob = dec->probs.coeff[pelem][index < dec->probs.length[pelem] - 1 ? index : dec->probs.length[pelem] - 1];
            }
            else {
                prob = 128;
            }

            ac_get (ac, gb, prob, &residual);
            v = ((predict >> 15) ^ residual) & 1;
            dsd_out[(i >> 3) * channels + ch] |= v << (7 - (i & 7));

            wr64le (status + 8, (rd64le (status + 8) << 1) | ((rd64le (status) >> 63) & 1));
            wr64le (status, (rd64le (status) << 1) | (uint64_t)v);
        }
    }

    return 0;

fail:
    fprintf (stderr, "ERROR in dst_decoder(ffmpeg): frame %llu substituted with silence\n", frame_no);
    return ret;
}

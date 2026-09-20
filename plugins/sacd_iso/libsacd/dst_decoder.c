// Thin wrapper around the DST decoder core ported from sacd-ripper
// (libs/libdstdec, GPLv2). See libsacd/dst/ for the original code
// and copyright notices.
#include <stdlib.h>
#include <string.h>
#include "dst_decoder.h"
#include "dst/dst_init.h"
#include "dst/dst_fram.h"

struct dst_decoder_s {
    ebunch bunch;
    int channel_count;
    int frame_count;
    size_t dsd_frame_size;   // decoded DSD bytes per frame (all channels)
};

dst_decoder_t *
dst_decoder_create (int channel_count) {
    if (channel_count <= 0 || channel_count > 6) {
        return NULL;
    }
    dst_decoder_t *dec = calloc (1, sizeof (*dec));
    if (!dec) {
        return NULL;
    }
    dec->channel_count = channel_count;
    // 64 = DSD64 rate multiplier; MaxFrameLen becomes 4704 bytes/channel
    if (DST_InitDecoder (&dec->bunch, channel_count, 64) != 0) {
        free (dec);
        return NULL;
    }
    dec->dsd_frame_size = dec->bunch.FrameHdr.MaxFrameLen * channel_count;
    return dec;
}

void
dst_decoder_destroy (dst_decoder_t *dec) {
    if (!dec) {
        return;
    }
    DST_CloseDecoder (&dec->bunch);
    free (dec);
}

int
dst_decoder_decode (dst_decoder_t *dec, const uint8_t *dst_data, size_t dst_size, uint8_t *dsd_out, size_t *dsd_out_size) {
    if (!dec || !dst_data || !dsd_out || !dsd_out_size) {
        return -1;
    }
    if (*dsd_out_size < dec->dsd_frame_size) {
        return -1;
    }
    int err = DST_FramDSTDecode ((uint8_t *)dst_data, dsd_out, (int)dst_size, dec->frame_count, &dec->bunch);
    if (err != 0) {
        return -1;
    }
    dec->frame_count++;
    *dsd_out_size = dec->dsd_frame_size;
    return 0;
}

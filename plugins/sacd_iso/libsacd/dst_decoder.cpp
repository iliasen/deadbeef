// Thin C++ wrapper around the reference DST decoder core
// (dst::decoder_t from foo_input_sacd's libdstdec, LGPL-2.1).
// See libsacd/dst2/ for the decoder and its copyright notices.
#include <new>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstddef>
#include "dst_decoder.h"
#include "dst2/decoder/decoder.h"

// libdstdec reports most frame-header problems only through log_printf()
// (see fr.h / stream.h) and still returns 0 from run(), passing the garbage
// through. The reference C decoder treats such frames as errors and
// substitutes DSD silence, so we count error logs emitted during run()
// and fail the frame if any occurred.
static int g_error_log_count;

// Referenced by the DST decoder core (decoder, fr, stream) through common.h.
void log_printf (const char *fmt, ...) {
    if (strncmp (fmt, "ERROR", 5) == 0) {
        g_error_log_count++;
    }
    va_list args;
    va_start (args, fmt);
    vfprintf (stderr, fmt, args);
    va_end (args);
    fputc ('\n', stderr);
}

struct dst_decoder_s {
    dst::decoder_t decoder;
    int channel_count;
    size_t dsd_frame_size;   // decoded DSD bytes per frame (all channels)
    unsigned long long frame_count;
};

dst_decoder_t *
dst_decoder_create (int channel_count) {
    if (channel_count <= 0 || channel_count > 6) {
        return NULL;
    }
    dst_decoder_t *dec = new (std::nothrow) dst_decoder_t ();
    if (!dec) {
        return NULL;
    }
    dec->channel_count = channel_count;
    if (dec->decoder.init (channel_count, DST_FRAME_SIZE) != 0) {
        delete dec;
        return NULL;
    }
    dec->dsd_frame_size = (size_t)DST_FRAME_SIZE * channel_count;
    dec->frame_count = 0;
    return dec;
}

void
dst_decoder_destroy (dst_decoder_t *dec) {
    delete dec;
}

int
dst_decoder_decode (dst_decoder_t *dec, const uint8_t *dst_data, size_t dst_size, uint8_t *dsd_out, size_t *dsd_out_size) {
    if (!dec || !dst_data || !dsd_out || !dsd_out_size) {
        return -1;
    }
    // Always report a full output frame so the caller never loses a frame;
    // on error the caller substitutes DSD silence.
    if (*dsd_out_size < dec->dsd_frame_size) {
        return -1;
    }
    g_error_log_count = 0;
    int err = dec->decoder.run (dst_data, (unsigned int)(dst_size * 8), dsd_out);
    *dsd_out_size = dec->dsd_frame_size;
    unsigned long long frame_no = dec->frame_count++;
    if (err != 0 || g_error_log_count != 0) {
        fprintf (stderr, "ERROR in dst_decoder: frame %llu substituted with silence\n", frame_no);
        return -1;
    }
    return 0;
}
#ifndef DSD2PCM_H_INCLUDED
#define DSD2PCM_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dsd2pcm_s dsd2pcm_t;

// decim: decimation factor (32 for DSD64 -> 88200 Hz)
// Creates a polyphase windowed-sinc FIR decimator, one instance handles
// `channels` interleaved channels.
dsd2pcm_t *dsd2pcm_create(int channels, int decim);
void dsd2pcm_destroy(dsd2pcm_t *t);
void dsd2pcm_reset(dsd2pcm_t *t);

// Feeds `nbytes` of byte-interleaved DSD data (MSB of each byte = first sample),
// writes up to `max_frames` interleaved float PCM frames into `out`.
// Returns the number of PCM frames written.
// Input length must be a multiple of `channels`.
size_t dsd2pcm_process(dsd2pcm_t *t, const uint8_t *dsd, size_t nbytes,
                       float *out, size_t max_frames);

// Output sample rate for a given DSD input rate
static inline int dsd2pcm_output_rate(int dsd_rate, int decim) {
    return dsd_rate / decim;
}

#ifdef __cplusplus
}
#endif

#endif

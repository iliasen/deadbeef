#include "dsd2pcm.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Taps per decimation phase. Total FIR length = decim * TAPS_PER_PHASE.
// 32 taps/phase @ decim 32 = 1024 taps: transition band ~4/1024*2822.4kHz ~ 11kHz,
// which with a 30kHz cutoff keeps the audio band (<20kHz) intact and still
// rejects most of the shaped ultrasonic DSD noise.
#define TAPS_PER_PHASE 32

// Lowpass cutoff in Hz (must be below output Nyquist = dsd_rate/decim/2)
#define CUTOFF_HZ 30000.0

struct dsd2pcm_s {
    int channels;
    int decim;
    int ntaps;          // decim * TAPS_PER_PHASE
    int phase;          // input samples mod decim
    int pos;            // ring buffer write position
    float *coeffs;      // ntaps
    float **hist;       // per-channel ring buffers, ntaps each
};

static void
design_filter(float *coeffs, int ntaps, double cutoff_hz, double dsd_rate) {
    // Windowed-sinc lowpass, Blackman window
    double fc = cutoff_hz / dsd_rate; // normalized to input rate
    int m = ntaps - 1;
    for (int i = 0; i < ntaps; i++) {
        double x = i - m / 2.0;
        double sinc = (x == 0.0) ? 2.0 * M_PI * fc : sin(2.0 * M_PI * fc * x) / x;
        double win = 0.42 - 0.5 * cos(2.0 * M_PI * i / m) + 0.08 * cos(4.0 * M_PI * i / m);
        coeffs[i] = (float)(sinc * win);
    }
    // normalize to unity gain at DC
    double sum = 0.0;
    for (int i = 0; i < ntaps; i++) {
        sum += coeffs[i];
    }
    for (int i = 0; i < ntaps; i++) {
        coeffs[i] = (float)(coeffs[i] / sum);
    }
}

dsd2pcm_t *
dsd2pcm_create(int channels, int decim) {
    if (channels <= 0 || decim <= 0 || (decim % 8) != 0) {
        return NULL;
    }
    dsd2pcm_t *t = calloc(1, sizeof(*t));
    if (!t) {
        return NULL;
    }
    t->channels = channels;
    t->decim = decim;
    t->ntaps = decim * TAPS_PER_PHASE;
    t->coeffs = malloc(t->ntaps * sizeof(float));
    t->hist = calloc(channels, sizeof(float *));
    if (!t->coeffs || !t->hist) {
        dsd2pcm_destroy(t);
        return NULL;
    }
    for (int ch = 0; ch < channels; ch++) {
        t->hist[ch] = calloc(t->ntaps, sizeof(float));
        if (!t->hist[ch]) {
            dsd2pcm_destroy(t);
            return NULL;
        }
    }
    design_filter(t->coeffs, t->ntaps, CUTOFF_HZ, 2822400.0);
    return t;
}

void
dsd2pcm_destroy(dsd2pcm_t *t) {
    if (!t) {
        return;
    }
    if (t->hist) {
        for (int ch = 0; ch < t->channels; ch++) {
            free(t->hist[ch]);
        }
        free(t->hist);
    }
    free(t->coeffs);
    free(t);
}

void
dsd2pcm_reset(dsd2pcm_t *t) {
    t->phase = 0;
    t->pos = 0;
    for (int ch = 0; ch < t->channels; ch++) {
        memset(t->hist[ch], 0, t->ntaps * sizeof(float));
    }
}

size_t
dsd2pcm_process(dsd2pcm_t *t, const uint8_t *dsd, size_t nbytes, float *out, size_t max_frames) {
    size_t frames = 0;
    int ntaps = t->ntaps;
    int decim = t->decim;
    int channels = t->channels;

    // Channels are byte-interleaved and processed in lockstep:
    // each group of `channels` bytes is one timeslot of 8 samples per channel.
    for (size_t i = 0; i + channels <= nbytes; i += channels) {
        for (int bit = 7; bit >= 0; bit--) {
            for (int ch = 0; ch < channels; ch++) {
                // MSB first: bit 7 is the oldest sample
                t->hist[ch][t->pos] = (dsd[i + ch] & (1 << bit)) ? 1.0f : -1.0f;
            }

            if (++t->phase == decim) {
                t->phase = 0;
                if (frames < max_frames) {
                    for (int ch = 0; ch < channels; ch++) {
                        // dot product of the ring buffer with the FIR coefficients
                        const float *h = t->hist[ch];
                        int p = t->pos;
                        float acc = 0.0f;
                        for (int k = 0; k < ntaps; k++) {
                            acc += t->coeffs[k] * h[p];
                            if (--p < 0) {
                                p = ntaps - 1;
                            }
                        }
                        out[frames * channels + ch] = acc;
                    }
                    frames++;
                }
            }

            if (++t->pos == ntaps) {
                t->pos = 0;
            }
        }
    }
    return frames;
}

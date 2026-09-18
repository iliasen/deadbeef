#include <stdlib.h>
#include <string.h>
#include "dst_decoder.h"

#ifdef HAVE_FFMPEG
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>

struct dst_decoder_s {
    AVCodecContext *ctx;
    AVFrame *frame;
    AVPacket *pkt;
};

dst_decoder_t *dst_decoder_create(int channel_count) {
    dst_decoder_t *dec = calloc(1, sizeof(*dec));
    if (!dec) return NULL;

    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_DST);
    if (!codec) {
        free(dec);
        return NULL;
    }

    dec->ctx = avcodec_alloc_context3(codec);
    if (!dec->ctx) {
        free(dec);
        return NULL;
    }

    dec->ctx->channels = channel_count;
    dec->ctx->sample_rate = 2822400;
    dec->ctx->sample_fmt = AV_SAMPLE_FMT_S32;

    if (avcodec_open2(dec->ctx, codec, NULL) < 0) {
        avcodec_free_context(&dec->ctx);
        free(dec);
        return NULL;
    }

    dec->frame = av_frame_alloc();
    dec->pkt = av_packet_alloc();
    return dec;
}

void dst_decoder_destroy(dst_decoder_t *dec) {
    if (!dec) return;
    if (dec->ctx) avcodec_free_context(&dec->ctx);
    if (dec->frame) av_frame_free(&dec->frame);
    if (dec->pkt) av_packet_free(&dec->pkt);
    free(dec);
}

int dst_decoder_decode(dst_decoder_t *dec, const uint8_t *dst_data, size_t dst_size, uint8_t *dsd_out, size_t *dsd_out_size) {
    if (!dec || !dst_data || !dsd_out || !dsd_out_size) return -1;

    dec->pkt->data = (uint8_t *)dst_data;
    dec->pkt->size = dst_size;

    int ret = avcodec_send_packet(dec->ctx, dec->pkt);
    if (ret < 0) return ret;

    ret = avcodec_receive_frame(dec->ctx, dec->frame);
    if (ret < 0) return ret;

    int frame_size = dec->frame->nb_samples * dec->ctx->channels * 4;
    if (*dsd_out_size < (size_t)frame_size) return -1;

    for (int ch = 0; ch < dec->ctx->channels; ch++) {
        int32_t *src = (int32_t *)dec->frame->extended_data[ch];
        uint8_t *dst = dsd_out + ch;
        for (int s = 0; s < dec->frame->nb_samples; s++) {
            int32_t sample = src[s];
            *dst = (sample >> 24) & 0xFF;
            dst += dec->ctx->channels;
        }
    }
    *dsd_out_size = frame_size;
    return 0;
}

#else

struct dst_decoder_s {
    int channel_count;
};

dst_decoder_t *dst_decoder_create(int channel_count) {
    dst_decoder_t *dec = calloc(1, sizeof(*dec));
    if (!dec) return NULL;
    dec->channel_count = channel_count;
    return dec;
}

void dst_decoder_destroy(dst_decoder_t *dec) {
    if (dec) free(dec);
}

int dst_decoder_decode(dst_decoder_t *dec, const uint8_t *dst_data, size_t dst_size, uint8_t *dsd_out, size_t *dsd_out_size) {
    (void)dec; (void)dst_data; (void)dst_size; (void)dsd_out; (void)dsd_out_size;
    return -1;
}

#endif
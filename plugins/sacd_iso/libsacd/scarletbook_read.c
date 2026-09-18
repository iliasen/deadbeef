#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "scarletbook.h"
#include "scarletbook_read.h"
#include "endianess.h"

#define AUDIO_SECTOR_HEADER_SIZE 1
#define AUDIO_PACKET_INFO_SIZE 2
#define AUDIO_FRAME_INFO_SIZE 4
#define MAX_PACKET_SIZE 2045
#define MAX_DST_SIZE (1024 * 64)
#define FRAME_SIZE_64 4704
#define SACD_FRAME_RATE 75

#define DATA_TYPE_AUDIO 2
#define DATA_TYPE_SUPPLEMENTARY 3
#define DATA_TYPE_PADDING 7

static inline int get_channel_count(const audio_frame_info_t *fi) {
    if (fi->channel_bit_2 == 1 && fi->channel_bit_3 == 0) return 6;
    if (fi->channel_bit_2 == 0 && fi->channel_bit_3 == 1) return 5;
    return 2;
}

static inline void exec_callback(scarletbook_handle_t *h, frame_read_callback_t cb, void *ud) {
    h->frame.started = 0;
    cb(h, h->frame.data, h->frame.size, ud);
}

void scarletbook_frame_init(scarletbook_handle_t *handle) {
    handle->frame_info_idx = 0;
    handle->frame.size = 0;
    handle->frame.started = 0;
    handle->frame.sector_count = 0;
    handle->frame.channel_count = 0;
    handle->frame.dst_encoded = 0;
    handle->frame.timecode.minutes = 0;
    handle->frame.timecode.seconds = 0;
    handle->frame.timecode.frames = 0;
    memset(&handle->audio_sector, 0, sizeof(audio_sector_t));
}

int scarletbook_process_frames(scarletbook_handle_t *handle, uint8_t *read_buffer, int blocks_read_in, int last_block, frame_read_callback_t frame_read_callback, void *userdata) {
    int nr_frames_processed = 0;
    int sector_bad_reads = 0;
    uint8_t *block_ptr = read_buffer;

    for (int j = 0; j < blocks_read_in; j++) {
        memcpy(&handle->audio_sector.header, block_ptr, AUDIO_SECTOR_HEADER_SIZE);
        block_ptr += AUDIO_SECTOR_HEADER_SIZE;

        for (uint8_t i = 0; i < handle->audio_sector.header.packet_info_count; i++) {
            handle->audio_sector.packet[i].frame_start = (block_ptr[0] >> 7) & 1;
            handle->audio_sector.packet[i].data_type = (block_ptr[0] >> 3) & 7;
            handle->audio_sector.packet[i].packet_length = ((block_ptr[0] & 7) << 8) | block_ptr[1];
            block_ptr += AUDIO_PACKET_INFO_SIZE;
        }

        if (handle->audio_sector.header.dst_encoded) {
            for (uint8_t i = 0; i < handle->audio_sector.header.frame_info_count; i++) {
                memcpy(&handle->audio_sector.frame[i], block_ptr, AUDIO_FRAME_INFO_SIZE);
                block_ptr += AUDIO_FRAME_INFO_SIZE;
            }
        } else {
            for (uint8_t i = 0; i < handle->audio_sector.header.frame_info_count; i++) {
                memcpy(&handle->audio_sector.frame[i], block_ptr, AUDIO_FRAME_INFO_SIZE - 1);
                block_ptr += AUDIO_FRAME_INFO_SIZE - 1;
            }
        }

        if (handle->audio_sector.header.packet_info_count > 7) {
            sector_bad_reads = 1;
            handle->frame.started = 0;
        }

        handle->frame_info_idx = 0;
        for (uint8_t pi = 0; pi < handle->audio_sector.header.packet_info_count; pi++) {
            audio_packet_info_t *pkt = &handle->audio_sector.packet[pi];
            if (pkt->packet_length > MAX_PACKET_SIZE) {
                sector_bad_reads = 1;
                block_ptr += pkt->packet_length;
                continue;
            }

            switch (pkt->data_type) {
                case DATA_TYPE_AUDIO:
                    if (pkt->frame_start) {
                        if (handle->frame.started && handle->frame.size > 0) {
                            if ((handle->frame.dst_encoded && handle->frame.sector_count == 0) ||
                                (!handle->frame.dst_encoded && handle->frame.size == handle->frame.channel_count * FRAME_SIZE_64)) {
                                exec_callback(handle, frame_read_callback, userdata);
                                nr_frames_processed++;
                            }
                        }
                        handle->frame.size = 0;
                        handle->frame.dst_encoded = handle->audio_sector.header.dst_encoded;
                        handle->frame.sector_count = handle->audio_sector.frame[handle->frame_info_idx].sector_count;
                        handle->frame.channel_count = get_channel_count(&handle->audio_sector.frame[handle->frame_info_idx]);
                        handle->frame.started = 1;
                        handle->frame.timecode = handle->audio_sector.frame[handle->frame_info_idx].timecode;
                        handle->frame_info_idx++;
                    }
                    if (handle->frame.started) {
                        if (handle->frame.size + pkt->packet_length <= MAX_DST_SIZE) {
                            memcpy(handle->frame.data + handle->frame.size, block_ptr, pkt->packet_length);
                            handle->frame.size += pkt->packet_length;
                            if (handle->frame.dst_encoded) handle->frame.sector_count--;
                        } else {
                            sector_bad_reads = 1;
                            handle->frame.started = 0;
                        }
                    }
                    break;
                case DATA_TYPE_SUPPLEMENTARY:
                case DATA_TYPE_PADDING:
                    break;
            }
            block_ptr += pkt->packet_length;
        }
        block_ptr = read_buffer + (j + 1) * SACD_LSN_SIZE;
    }

    if (last_block && handle->frame.started && handle->frame.size > 0) {
        if ((handle->frame.dst_encoded && handle->frame.sector_count == 0) ||
            (!handle->frame.dst_encoded && handle->frame.size == handle->frame.channel_count * FRAME_SIZE_64)) {
            exec_callback(handle, frame_read_callback, userdata);
            nr_frames_processed++;
        }
    }

    return sector_bad_reads ? -1 : nr_frames_processed;
}
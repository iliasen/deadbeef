#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "scarletbook.h"
#include "scarletbook_read.h"
#include "sacd_reader.h"
#include "endianess.h"

#define MAX_DST_SIZE (1024 * 64)

scarletbook_handle_t *scarletbook_open(sacd_reader_t *sacd) {
    scarletbook_handle_t *sb = calloc(1, sizeof(*sb));
    if (!sb) return NULL;

    sb->frame.data = malloc(MAX_DST_SIZE);
    if (!sb->frame.data) {
        free(sb);
        return NULL;
    }

    sb->sacd = sacd;
    sb->twoch_area_idx = -1;
    sb->mulch_area_idx = -1;

    if (!scarletbook_read_master_toc(sb)) {
        free(sb->frame.data);
        free(sb);
        return NULL;
    }

    if (sb->master_toc->area_1_toc_1_start > 0) {
        int flag_use_toc1 = 0;
        sb->area[sb->area_count].area_data = malloc(sb->master_toc->area_1_toc_size * SACD_LSN_SIZE);
        if (sb->area[sb->area_count].area_data) {
            if (!sacd_read_block_raw(sacd, sb->master_toc->area_1_toc_1_start,
                                     sb->master_toc->area_1_toc_size,
                                     sb->area[sb->area_count].area_data)) {
                flag_use_toc1 = 0;
            } else {
                flag_use_toc1 = 1;
            }

            if (sb->master_toc->area_1_toc_2_start > 0) {
                uint8_t *toc2 = malloc(sb->master_toc->area_1_toc_size * SACD_LSN_SIZE);
                if (toc2 && sacd_read_block_raw(sacd, sb->master_toc->area_1_toc_2_start,
                                                sb->master_toc->area_1_toc_size, toc2)) {
                    if (memcmp(sb->area[sb->area_count].area_data, toc2,
                               sb->master_toc->area_1_toc_size * SACD_LSN_SIZE) == 0) {
                        flag_use_toc1 = 1;
                    }
                }
                free(toc2);
            }

            if (flag_use_toc1 && scarletbook_read_area_toc(sb, sb->area_count)) {
                sb->area_count++;
            } else {
                free(sb->area[sb->area_count].area_data);
                sb->area[sb->area_count].area_data = NULL;
            }
        }
    }

    if (sb->master_toc->area_2_toc_1_start > 0) {
        int flag_use_toc1 = 0;
        sb->area[sb->area_count].area_data = malloc(sb->master_toc->area_2_toc_size * SACD_LSN_SIZE);
        if (sb->area[sb->area_count].area_data) {
            if (!sacd_read_block_raw(sacd, sb->master_toc->area_2_toc_1_start,
                                     sb->master_toc->area_2_toc_size,
                                     sb->area[sb->area_count].area_data)) {
                flag_use_toc1 = 0;
            } else {
                flag_use_toc1 = 1;
            }

            if (sb->master_toc->area_2_toc_2_start > 0) {
                uint8_t *toc2 = malloc(sb->master_toc->area_2_toc_size * SACD_LSN_SIZE);
                if (toc2 && sacd_read_block_raw(sacd, sb->master_toc->area_2_toc_2_start,
                                                sb->master_toc->area_2_toc_size, toc2)) {
                    if (memcmp(sb->area[sb->area_count].area_data, toc2,
                               sb->master_toc->area_2_toc_size * SACD_LSN_SIZE) == 0) {
                        flag_use_toc1 = 1;
                    }
                }
                free(toc2);
            }

            if (flag_use_toc1 && scarletbook_read_area_toc(sb, sb->area_count)) {
                sb->area_count++;
            } else {
                free(sb->area[sb->area_count].area_data);
                sb->area[sb->area_count].area_data = NULL;
            }
        }
    }

    if (sb->area_count == 0) {
        free(sb->frame.data);
        free(sb);
        return NULL;
    }

    return sb;
}

static void free_area(scarletbook_area_t *area) {
    if (!area || !area->area_toc) return;
    for (int i = 0; i < area->area_toc->track_count; i++) {
        free(area->area_track_text[i].track_type_title);
        free(area->area_track_text[i].track_type_performer);
        free(area->area_track_text[i].track_type_songwriter);
        free(area->area_track_text[i].track_type_composer);
        free(area->area_track_text[i].track_type_arranger);
        free(area->area_track_text[i].track_type_message);
        free(area->area_track_text[i].track_type_extra_message);
        free(area->area_track_text[i].track_type_title_phonetic);
        free(area->area_track_text[i].track_type_performer_phonetic);
        free(area->area_track_text[i].track_type_songwriter_phonetic);
        free(area->area_track_text[i].track_type_composer_phonetic);
        free(area->area_track_text[i].track_type_arranger_phonetic);
        free(area->area_track_text[i].track_type_message_phonetic);
        free(area->area_track_text[i].track_type_extra_message_phonetic);
    }
    free(area->description);
    free(area->copyright);
    free(area->description_phonetic);
    free(area->copyright_phonetic);
}

void scarletbook_close(scarletbook_handle_t *handle) {
    if (!handle) return;
    if (has_two_channel(handle)) {
        free_area(&handle->area[handle->twoch_area_idx]);
        free(handle->area[handle->twoch_area_idx].area_data);
    }
    if (has_multi_channel(handle)) {
        free_area(&handle->area[handle->mulch_area_idx]);
        free(handle->area[handle->mulch_area_idx].area_data);
    }
    master_text_t *mt = &handle->master_text;
    free(mt->album_title); free(mt->album_title_phonetic);
    free(mt->album_artist); free(mt->album_artist_phonetic);
    free(mt->album_publisher); free(mt->album_publisher_phonetic);
    free(mt->album_copyright); free(mt->album_copyright_phonetic);
    free(mt->disc_title); free(mt->disc_title_phonetic);
    free(mt->disc_artist); free(mt->disc_artist_phonetic);
    free(mt->disc_publisher); free(mt->disc_publisher_phonetic);
    free(mt->disc_copyright); free(mt->disc_copyright_phonetic);
    if (handle->master_data) free(handle->master_data);
    if (handle->frame.data) free(handle->frame.data);
    free(handle);
}

static int scarletbook_read_master_toc(scarletbook_handle_t *handle) {
    handle->master_data = malloc(MASTER_TOC_LEN * SACD_LSN_SIZE);
    if (!handle->master_data) return 0;

    if (!sacd_read_block_raw(handle->sacd, START_OF_MASTER_TOC, MASTER_TOC_LEN, handle->master_data))
        return 0;

    master_toc_t *master_toc = handle->master_toc = (master_toc_t *)handle->master_data;

    if (strncmp("SACDMTOC", master_toc->id, 8) != 0) return 0;

    SWAP16(master_toc->album_set_size);
    SWAP16(master_toc->album_sequence_number);
    SWAP32(master_toc->area_1_toc_1_start);
    SWAP32(master_toc->area_1_toc_2_start);
    SWAP16(master_toc->area_1_toc_size);
    SWAP32(master_toc->area_2_toc_1_start);
    SWAP32(master_toc->area_2_toc_2_start);
    SWAP16(master_toc->area_2_toc_size);
    SWAP16(master_toc->disc_date_year);

    if (master_toc->version.major > SUPPORTED_VERSION_MAJOR ||
        master_toc->version.minor > SUPPORTED_VERSION_MINOR)
        return 0;

    uint8_t *p = handle->master_data + SACD_LSN_SIZE;
    for (int i = 0; i < MAX_LANGUAGE_COUNT; i++) {
        master_sacd_text_t *master_text = (master_sacd_text_t *)p;
        if (strncmp("SACDText", master_text->id, 8) != 0) return 0;

        SWAP16(master_text->album_title_position);
        SWAP16(master_text->album_title_phonetic_position);
        SWAP16(master_text->album_artist_position);
        SWAP16(master_text->album_artist_phonetic_position);
        SWAP16(master_text->album_publisher_position);
        SWAP16(master_text->album_publisher_phonetic_position);
        SWAP16(master_text->album_copyright_position);
        SWAP16(master_text->album_copyright_phonetic_position);
        SWAP16(master_text->disc_title_position);
        SWAP16(master_text->disc_title_phonetic_position);
        SWAP16(master_text->disc_artist_position);
        SWAP16(master_text->disc_artist_phonetic_position);
        SWAP16(master_text->disc_publisher_position);
        SWAP16(master_text->disc_publisher_phonetic_position);
        SWAP16(master_text->disc_copyright_position);
        SWAP16(master_text->disc_copyright_phonetic_position);

        if (i == 0) {
            const char *cs = character_set[handle->master_toc->locales[i].character_set & 0x07];
#define COPY_TEXT(field, pos) \
    if (master_text->pos) \
        handle->master_text.field = strdup((char *)master_text + master_text->pos)

            COPY_TEXT(album_title, album_title_position);
            COPY_TEXT(album_title_phonetic, album_title_phonetic_position);
            COPY_TEXT(album_artist, album_artist_position);
            COPY_TEXT(album_artist_phonetic, album_artist_phonetic_position);
            COPY_TEXT(album_publisher, album_publisher_position);
            COPY_TEXT(album_publisher_phonetic, album_publisher_phonetic_position);
            COPY_TEXT(album_copyright, album_copyright_position);
            COPY_TEXT(album_copyright_phonetic, album_copyright_phonetic_position);
            COPY_TEXT(disc_title, disc_title_position);
            COPY_TEXT(disc_title_phonetic, disc_title_phonetic_position);
            COPY_TEXT(disc_artist, disc_artist_position);
            COPY_TEXT(disc_artist_phonetic, disc_artist_phonetic_position);
            COPY_TEXT(disc_publisher, disc_publisher_position);
            COPY_TEXT(disc_publisher_phonetic, disc_publisher_phonetic_position);
            COPY_TEXT(disc_copyright, disc_copyright_position);
            COPY_TEXT(disc_copyright_phonetic, disc_copyright_phonetic_position);
#undef COPY_TEXT
        }
        p += SACD_LSN_SIZE;
    }

    handle->master_man = (master_man_t *)p;
    return 1;
}

static int scarletbook_read_area_toc(scarletbook_handle_t *handle, int area_idx) {
    scarletbook_area_t *area = &handle->area[area_idx];
    area_toc_t *area_toc = area->area_toc = (area_toc_t *)area->area_data;

    if (strncmp("TWOCHTOC", area_toc->id, 8) != 0 &&
        strncmp("MULCHTOC", area_toc->id, 8) != 0)
        return 0;

    SWAP16(area_toc->size);
    SWAP32(area_toc->track_start);
    SWAP32(area_toc->track_end);
    SWAP16(area_toc->area_description_offset);
    SWAP16(area_toc->copyright_offset);
    SWAP16(area_toc->area_description_phonetic_offset);
    SWAP16(area_toc->copyright_phonetic_offset);
    SWAP32(area_toc->max_byte_rate);
    SWAP16(area_toc->track_text_offset);
    SWAP16(area_toc->index_list_offset);
    SWAP16(area_toc->access_list_offset);

    const char *cs = character_set[area_toc->languages[0].character_set & 0x07];

#define COPY_AREA_TEXT(field, pos) \
    if (area_toc->pos) \
        area->field = strdup((char *)area_toc + area_toc->pos)

    COPY_AREA_TEXT(description, area_description_offset);
    COPY_AREA_TEXT(copyright, copyright_offset);
    COPY_AREA_TEXT(description_phonetic, area_description_phonetic_offset);
    COPY_AREA_TEXT(copyright_phonetic, copyright_phonetic_offset);
#undef COPY_AREA_TEXT

    if (area_toc->version.major > SUPPORTED_VERSION_MAJOR ||
        area_toc->version.minor > SUPPORTED_VERSION_MINOR)
        return 0;

    if (area_toc->channel_count == 2 && area_toc->loudspeaker_config == 0)
        handle->twoch_area_idx = area_idx;
    else
        handle->mulch_area_idx = area_idx;

    uint8_t *p = area->area_data + SACD_LSN_SIZE;
    uint8_t *end = area->area_data + area_toc->size * SACD_LSN_SIZE;
    int sacd_text_idx = 0;

    while (p < end) {
        if (strncmp((char *)p, "SACDTTxt", 8) == 0) {
            if (sacd_text_idx == 0) {
                area_text_t *area_text = area->area_text = (area_text_t *)p;
                for (int i = 0; i < area_toc->track_count; i++) {
                    SWAP16(area_text->track_text_position[i]);
                    if (area_text->track_text_position[i] > 0) {
                        char *track_ptr = (char *)p + area_text->track_text_position[i];
                        uint8_t track_amount = *track_ptr;
                        track_ptr += 4;
                        for (int j = 0; j < track_amount; j++) {
                            uint8_t track_type = *track_ptr++;
                            track_ptr++;
                            if (*track_ptr) {
                                int len = strlen(track_ptr);
                                char *converted = strdup(track_ptr);
#define SET_TEXT(type, field) \
    case type: \
        area->area_track_text[i].field = converted; break
                                switch (track_type) {
                                    SET_TEXT(0x01, track_type_title);
                                    SET_TEXT(0x02, track_type_performer);
                                    SET_TEXT(0x03, track_type_songwriter);
                                    SET_TEXT(0x04, track_type_composer);
                                    SET_TEXT(0x05, track_type_arranger);
                                    SET_TEXT(0x06, track_type_message);
                                    SET_TEXT(0x07, track_type_extra_message);
                                    SET_TEXT(0x81, track_type_title_phonetic);
                                    SET_TEXT(0x82, track_type_performer_phonetic);
                                    SET_TEXT(0x83, track_type_songwriter_phonetic);
                                    SET_TEXT(0x84, track_type_composer_phonetic);
                                    SET_TEXT(0x85, track_type_arranger_phonetic);
                                    SET_TEXT(0x86, track_type_message_phonetic);
                                    SET_TEXT(0x87, track_type_extra_message_phonetic);
                                    default: free(converted); break;
                                }
#undef SET_TEXT
                            }
                            if (j < track_amount - 1) {
                                while (*track_ptr) track_ptr++;
                                while (!*track_ptr) track_ptr++;
                            }
                        }
                    }
                }
            }
            sacd_text_idx++;
            p += SACD_LSN_SIZE;
        } else if (strncmp((char *)p, "SACD_IGL", 8) == 0) {
            area->area_isrc_genre = (area_isrc_genre_t *)p;
            p += SACD_LSN_SIZE * 2;
        } else if (strncmp((char *)p, "SACD_ACC", 8) == 0) {
            p += SACD_LSN_SIZE * 32;
        } else if (strncmp((char *)p, "SACDTRL1", 8) == 0) {
            area->area_tracklist_offset = (area_tracklist_offset_t *)p;
            for (int i = 0; i < area_toc->track_count; i++) {
                SWAP32(area->area_tracklist_offset->track_start_lsn[i]);
                SWAP32(area->area_tracklist_offset->track_length_lsn[i]);
            }
            p += SACD_LSN_SIZE;
        } else if (strncmp((char *)p, "SACDTRL2", 8) == 0) {
            area->area_tracklist_time = (area_tracklist_t *)p;
            p += SACD_LSN_SIZE;
        } else {
            break;
        }
    }
    return 1;
}
#include <deadbeef/deadbeef.h>
#include <deadbeef/common.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <strings.h>

#include "libsacd/scarletbook.h"
#include "libsacd/sacd_reader.h"
#include "libsacd/scarletbook_read.h"
#include "libsacd/dst_decoder.h"

DB_functions_t *deadbeef;

static DB_vfs_t plugin;

struct sacd_track_info {
    int area_idx;
    int track_idx;
    uint32_t start_lsn;
    uint32_t length_lsn;
    int channel_count;
    int frame_format;
    int dst_encoded;
};

typedef struct {
    DB_FILE file;
    sacd_reader_t *sacd;
    scarletbook_handle_t *handle;
    struct sacd_track_info *track;
    uint8_t *read_buffer;
    uint32_t current_lsn;
    uint32_t end_lsn;
    dst_decoder_t *dst_dec;
    uint8_t *dsd_buffer;
    size_t dsd_buffer_size;
    size_t dsd_pos;
    size_t dsd_len;
} ddb_sacd_file_t;

static const char *sacd_schemes[] = { "sacd", NULL };

static const char **
sacd_get_schemes (void) {
    return sacd_schemes;
}

static int
sacd_is_streaming (void) {
    return 0;
}

static int
sacd_is_container (const char *fname) {
    if (!fname) return 0;
    const char *ext = strrchr(fname, '.');
    return ext && (strcasecmp(ext, ".iso") == 0 || strcasecmp(ext, ".ISO") == 0);
}

static int
sacd_parse_uri (const char *uri, char **out_path, int *out_area, int *out_track) {
    if (strncmp(uri, "sacd://", 7) != 0) return -1;
    const char *p = uri + 7;
    char *path = strdup(p);
    if (!path) return -1;

    char *colon = strrchr(path, ':');
    if (colon) {
        *colon = '\0';
        *out_track = atoi(colon + 1);
    } else {
        *out_track = -1;
    }

    char *at = strrchr(path, '@');
    if (at) {
        *at = '\0';
        *out_area = atoi(at + 1);
    } else {
        *out_area = 0;
    }

    *out_path = path;
    return 0;
}

static DB_FILE *
sacd_open (const char *fname) {
    char *path = NULL;
    int area_idx = 0, track_idx = -1;
    if (sacd_parse_uri(fname, &path, &area_idx, &track_idx) < 0) {
        free(path);
        return NULL;
    }

    sacd_reader_t *sacd = sacd_open(path);
    if (!sacd) {
        free(path);
        return NULL;
    }

    scarletbook_handle_t *handle = scarletbook_open(sacd);
    if (!handle) {
        sacd_close(sacd);
        free(path);
        return NULL;
    }

    if (area_idx < 0 || area_idx >= handle->area_count) {
        scarletbook_close(handle);
        sacd_close(sacd);
        free(path);
        return NULL;
    }

    scarletbook_area_t *area = &handle->area[area_idx];
    if (track_idx >= 0) {
        if (track_idx >= area->area_toc->track_count) {
            scarletbook_close(handle);
            sacd_close(sacd);
            free(path);
            return NULL;
        }
    }

    ddb_sacd_file_t *f = calloc(1, sizeof(*f));
    f->file.vfs = &plugin;
    f->sacd = sacd;
    f->handle = handle;
    f->read_buffer = malloc(32 * SACD_LSN_SIZE);
    f->dsd_buffer_size = 64 * 4704 * 6;
    f->dsd_buffer = malloc(f->dsd_buffer_size);

    f->track = calloc(1, sizeof(*f->track));
    f->track->area_idx = area_idx;
    f->track->track_idx = track_idx;

    if (track_idx >= 0) {
        f->track->start_lsn = area->area_tracklist_offset->track_start_lsn[track_idx];
        f->track->length_lsn = area->area_tracklist_offset->track_length_lsn[track_idx];
        f->track->channel_count = area->area_toc->channel_count;
        f->track->frame_format = area->area_toc->frame_format;
        f->track->dst_encoded = (area->area_toc->frame_format == FRAME_FORMAT_DST);
        if (f->track->dst_encoded) {
            f->dst_dec = dst_decoder_create(f->track->channel_count);
        }
    } else {
        f->track->start_lsn = area->area_toc->track_start;
        f->track->length_lsn = area->area_toc->track_end - area->area_toc->track_start;
    }

    f->current_lsn = f->track->start_lsn;
    f->end_lsn = f->track->start_lsn + f->track->length_lsn;

    return (DB_FILE*)f;
}

static void
sacd_close (DB_FILE *file) {
    ddb_sacd_file_t *f = (ddb_sacd_file_t *)file;
    if (f->sacd) sacd_close(f->sacd);
    if (f->handle) scarletbook_close(f->handle);
    free(f->track);
    free(f->read_buffer);
    if (f->dst_dec) dst_decoder_destroy(f->dst_dec);
    free(f->dsd_buffer);
    free(f);
}

static size_t
sacd_read (void *ptr, size_t size, size_t nmemb, DB_FILE *file) {
    ddb_sacd_file_t *f = (ddb_sacd_file_t *)file;
    size_t total = size * nmemb;
    if (total == 0) return 0;

    uint8_t *out = ptr;
    size_t done = 0;

    while (done < total) {
        if (f->dsd_pos >= f->dsd_len) {
            if (f->current_lsn >= f->end_lsn) break;

            int blocks_to_read = 32;
            if (f->current_lsn + blocks_to_read > f->end_lsn) {
                blocks_to_read = f->end_lsn - f->current_lsn;
            }

            uint32_t read_blocks = sacd_read_block_raw(f->sacd, f->current_lsn, blocks_to_read, f->read_buffer);
            if (read_blocks == 0) break;

            scarletbook_frame_init(f->handle);
            int frames = scarletbook_process_frames(f->handle, f->read_buffer, read_blocks,
                                                    f->current_lsn + read_blocks >= f->end_lsn,
                                                    NULL, NULL);
            (void)frames;

            f->current_lsn += read_blocks;

            if (f->track->dst_encoded && f->dst_dec && f->handle->frame.size > 0) {
                size_t out_size = f->dsd_buffer_size;
                int ret = dst_decoder_decode(f->dst_dec, f->handle->frame.data, f->handle->frame.size,
                                             f->dsd_buffer, &out_size);
                if (ret == 0) {
                    f->dsd_pos = 0;
                    f->dsd_len = out_size;
                } else {
                    f->dsd_len = 0;
                }
            } else if (!f->track->dst_encoded && f->handle->frame.size > 0) {
                f->dsd_len = f->handle->frame.size;
                if (f->dsd_len > f->dsd_buffer_size) f->dsd_len = f->dsd_buffer_size;
                memcpy(f->dsd_buffer, f->handle->frame.data, f->dsd_len);
                f->dsd_pos = 0;
            } else {
                f->dsd_len = 0;
            }
        }

        if (f->dsd_pos < f->dsd_len) {
            size_t avail = f->dsd_len - f->dsd_pos;
            size_t to_copy = total - done < avail ? total - done : avail;
            memcpy(out + done, f->dsd_buffer + f->dsd_pos, to_copy);
            f->dsd_pos += to_copy;
            done += to_copy;
        } else {
            break;
        }
    }

    return done / size;
}

static int
sacd_seek (DB_FILE *file, int64_t offset, int whence) {
    ddb_sacd_file_t *f = (ddb_sacd_file_t *)file;
    int64_t new_pos;

    switch (whence) {
        case SEEK_SET: new_pos = offset; break;
        case SEEK_CUR: new_pos = (int64_t)(f->current_lsn - f->track->start_lsn) * f->track->channel_count * FRAME_SIZE_64 + f->dsd_pos + offset; break;
        case SEEK_END: new_pos = (int64_t)f->track->length_lsn * f->track->channel_count * FRAME_SIZE_64 + offset; break;
        default: return -1;
    }

    if (new_pos < 0) new_pos = 0;
    uint64_t max_pos = (uint64_t)f->track->length_lsn * f->track->channel_count * FRAME_SIZE_64;
    if (new_pos > (int64_t)max_pos) new_pos = max_pos;

    uint32_t target_lsn = f->track->start_lsn + (uint32_t)(new_pos / (f->track->channel_count * FRAME_SIZE_64));
    if (target_lsn < f->track->start_lsn) target_lsn = f->track->start_lsn;
    if (target_lsn > f->end_lsn) target_lsn = f->end_lsn;

    f->current_lsn = target_lsn;
    f->dsd_pos = f->dsd_len = 0;
    return 0;
}

static int64_t
sacd_tell (DB_FILE *file) {
    ddb_sacd_file_t *f = (ddb_sacd_file_t *)file;
    return (int64_t)(f->current_lsn - f->track->start_lsn) * f->track->channel_count * FRAME_SIZE_64 + f->dsd_pos;
}

static void
sacd_rewind (DB_FILE *file) {
    ddb_sacd_file_t *f = (ddb_sacd_file_t *)file;
    f->current_lsn = f->track->start_lsn;
    f->dsd_pos = f->dsd_len = 0;
}

static int64_t
sacd_getlength (DB_FILE *file) {
    ddb_sacd_file_t *f = (ddb_sacd_file_t *)file;
    return (int64_t)f->track->length_lsn * f->track->channel_count * FRAME_SIZE_64;
}

static int
sacd_scandir (const char *dirname, struct dirent ***namelist, int (*selector)(const struct dirent *), int (*cmp)(const struct dirent **, const struct dirent **)) {
    (void)selector; (void)cmp;
    char *path = NULL;
    int area_idx = 0, track_idx = -1;
    if (sacd_parse_uri(dirname, &path, &area_idx, &track_idx) < 0) {
        free(path);
        return -1;
    }

    sacd_reader_t *sacd = sacd_open(path);
    free(path);
    if (!sacd) return -1;

    scarletbook_handle_t *handle = scarletbook_open(sacd);
    if (!handle) {
        sacd_close(sacd);
        return -1;
    }

    if (area_idx < 0 || area_idx >= handle->area_count) {
        scarletbook_close(handle);
        sacd_close(sacd);
        return -1;
    }

    scarletbook_area_t *area = &handle->area[area_idx];
    int count = area->area_toc->track_count;

    struct dirent **entries = calloc(count + 1, sizeof(*entries));
    for (int i = 0; i < count; i++) {
        entries[i] = calloc(1, sizeof(struct dirent));
        snprintf(entries[i]->d_name, sizeof(entries[i]->d_name), "track%02d.dsf", i + 1);
    }
    entries[count] = NULL;

    scarletbook_close(handle);
    sacd_close(sacd);

    *namelist = entries;
    return count;
}

static const char *
sacd_get_content_type (DB_FILE *file) {
    (void)file;
    return "audio/x-dsd";
}

static const char *
sacd_get_scheme_for_name (const char *fname) {
    if (sacd_is_container(fname)) {
        return "sacd://";
    }
    return NULL;
}

static void
sacd_set_track (DB_FILE *file, DB_playItem_t *it) {
    (void)file; (void)it;
}

static DB_vfs_t plugin = {
    .plugin = {
        DDB_PLUGIN_SET_API_VERSION
        .type = DB_PLUGIN_VFS,
        .id = "sacd_iso",
        .name = "SACD ISO",
        .descr = "Play SACD ISO images (DSD/DST)",
        .version_major = 0,
        .version_minor = 1,
        .flags = 0,
    },
    .get_schemes = sacd_get_schemes,
    .is_streaming = sacd_is_streaming,
    .is_container = sacd_is_container,
    .open = sacd_open,
    .close = sacd_close,
    .read = sacd_read,
    .seek = sacd_seek,
    .tell = sacd_tell,
    .rewind = sacd_rewind,
    .getlength = sacd_getlength,
    .scandir = sacd_scandir,
    .get_content_type = sacd_get_content_type,
    .set_track = sacd_set_track,
};

DB_plugin_t *
sacd_iso_load (DB_functions_t *api) {
    deadbeef = api;
    return DB_PLUGIN(&plugin);
}
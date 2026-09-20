#include <deadbeef/deadbeef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <strings.h>

#include "libsacd/scarletbook.h"
#include "libsacd/sacd_reader.h"
#include "libsacd/scarletbook_read.h"
#include "libsacd/dst_decoder.h"
#include "dsd2pcm.h"

DB_functions_t *deadbeef;

#define SECTORS_PER_READ 32
#define DECIM 32
#define PCM_RATE (SACD_SAMPLING_FREQUENCY / DECIM) // 88200

static DB_decoder_t plugin;

#define trace(...) deadbeef->log_detailed (&plugin.plugin, DDB_LOG_LAYER_INFO, __VA_ARGS__)

typedef struct {
    DB_fileinfo_t info;
    sacd_reader_t *sacd;
    scarletbook_handle_t *handle;
    int area_idx;
    int channels;

    uint32_t start_lsn;
    uint32_t length_lsn;
    uint32_t current_lsn;
    uint32_t end_lsn;

    dsd2pcm_t *d2p;
    dst_decoder_t *dst_dec;
    uint8_t *dst_dsd_buffer; // decoded-DSD scratch buffer for DST frames
    size_t dst_dsd_size;
    int is_dst;
    float duration_sec;

    uint8_t *read_buffer;    // SECTORS_PER_READ sectors of raw data
    uint8_t *dsd_queue;      // completed DSD frames waiting for decimation
    size_t dsd_queue_size;
    size_t dsd_queue_cap;

    float *pcm_buffer;       // decimated PCM waiting to be consumed by read()
    size_t pcm_frames;       // valid frames in pcm_buffer
    size_t pcm_pos;          // consumed frames
    size_t pcm_cap;          // capacity in frames

    int64_t skip_frames;     // PCM frames to drop (sample-accurate seek)
    int64_t frames_played;   // total PCM frames delivered to the streamer
    int eof;
} sacd_fileinfo_t;

// ---------------------------------------------------------------------------
// helpers

static int
pick_area (scarletbook_handle_t *handle) {
    // prefer stereo area, fall back to multichannel
    if (has_two_channel (handle)) {
        return handle->twoch_area_idx;
    }
    if (has_multi_channel (handle)) {
        return handle->mulch_area_idx;
    }
    return -1;
}

// Track duration in seconds. For DST areas the sector count is not linear
// in time (variable bitrate), so prefer the time-based tracklist (SACDTRL2).
static float
track_duration_sec (scarletbook_area_t *area, int track_idx) {
    if (area->area_tracklist_time) {
        area_tracklist_time_t *t = &area->area_tracklist_time->duration[track_idx];
        return t->minutes * 60.0f + t->seconds + t->frames / (float)SACD_FRAME_RATE;
    }
    return area->area_tracklist_offset->track_length_lsn[track_idx] / (float)SACD_FRAME_RATE;
}

static void
queue_append (sacd_fileinfo_t *info, const uint8_t *data, size_t size) {
    if (info->dsd_queue_size + size > info->dsd_queue_cap) {
        size_t newcap = info->dsd_queue_cap ? info->dsd_queue_cap * 2 : 1 << 16;
        if (newcap < info->dsd_queue_size + size) {
            newcap = info->dsd_queue_size + size;
        }
        uint8_t *nb = realloc (info->dsd_queue, newcap);
        if (!nb) {
            trace ("sacd_iso: out of memory growing dsd queue\n");
            return;
        }
        info->dsd_queue = nb;
        info->dsd_queue_cap = newcap;
    }
    memcpy (info->dsd_queue + info->dsd_queue_size, data, size);
    info->dsd_queue_size += size;
}

// Called by scarletbook_process_frames for each completed audio frame.
// DST frames are decoded to DSD first, raw DSD frames are queued as-is.
static void
frame_cb (scarletbook_handle_t *handle, uint8_t *data, int size, void *userdata) {
    sacd_fileinfo_t *info = userdata;
    if (info->is_dst) {
        size_t out_size = info->dst_dsd_size;
        if (dst_decoder_decode (info->dst_dec, data, (size_t)size, info->dst_dsd_buffer, &out_size) == 0) {
            queue_append (info, info->dst_dsd_buffer, out_size);
        }
        // decode errors: drop the frame
    }
    else {
        queue_append (info, data, (size_t)size);
    }
    (void)handle;
}

// Reads the next chunk of sectors, extracts DSD frames and decimates them
// into pcm_buffer. Returns 0 on success (pcm_frames may still be 0 due to
// filter warmup), -1 on end of stream / read error.
static int
fill (sacd_fileinfo_t *info) {
    if (info->current_lsn >= info->end_lsn) {
        return -1;
    }

    uint32_t blocks = SECTORS_PER_READ;
    if (info->current_lsn + blocks > info->end_lsn) {
        blocks = info->end_lsn - info->current_lsn;
    }

    uint32_t read_blocks = sacd_read_block_raw (info->sacd, info->current_lsn, blocks, info->read_buffer);
    if (read_blocks == 0) {
        return -1;
    }

    int last_block = (info->current_lsn + read_blocks >= info->end_lsn);
    scarletbook_process_frames (info->handle, info->read_buffer, read_blocks, last_block, frame_cb, info);
    info->current_lsn += read_blocks;

    // decimate as much of the queue as possible; the decimator needs
    // DECIM/8 bytes per channel per output frame
    size_t group = (size_t)info->channels * (DECIM / 8);
    size_t consumable = info->dsd_queue_size / group * group;
    if (consumable == 0) {
        return 0;
    }

    size_t max_frames = consumable / group;
    if (max_frames > info->pcm_cap) {
        float *nb = realloc (info->pcm_buffer, max_frames * info->channels * sizeof (float));
        if (!nb) {
            trace ("sacd_iso: out of memory growing pcm buffer\n");
            return -1;
        }
        info->pcm_buffer = nb;
        info->pcm_cap = max_frames;
    }

    size_t frames = dsd2pcm_process (info->d2p, info->dsd_queue, consumable, info->pcm_buffer, max_frames);

    // keep the unconsumed tail of the queue
    memmove (info->dsd_queue, info->dsd_queue + consumable, info->dsd_queue_size - consumable);
    info->dsd_queue_size -= consumable;

    info->pcm_frames = frames;
    info->pcm_pos = 0;
    return 0;
}

// ---------------------------------------------------------------------------
// decoder API

static DB_fileinfo_t *
sacd_dec_open (uint32_t hints) {
    (void)hints;
    sacd_fileinfo_t *info = calloc (1, sizeof (*info));
    return &info->info;
}

static int
sacd_dec_init (DB_fileinfo_t *_info, DB_playItem_t *it) {
    sacd_fileinfo_t *info = (sacd_fileinfo_t *)_info;

    deadbeef->pl_lock ();
    char *fname = strdup (deadbeef->pl_find_meta (it, ":URI"));
    deadbeef->pl_unlock ();

    int track_idx = deadbeef->pl_find_meta_int (it, ":TRACKNUM", 0);
    int area_idx = deadbeef->pl_find_meta_int (it, ":SACD_AREA", -1);

    info->sacd = sacd_open (fname);
    if (!info->sacd) {
        trace ("sacd_iso: failed to open %s\n", fname);
        free (fname);
        return -1;
    }

    info->handle = scarletbook_open (info->sacd);
    if (!info->handle) {
        trace ("sacd_iso: %s is not a valid SACD image\n", fname);
        free (fname);
        return -1;
    }

    if (area_idx < 0) {
        area_idx = pick_area (info->handle);
    }
    if (area_idx < 0 || area_idx >= info->handle->area_count) {
        trace ("sacd_iso: no usable area in %s\n", fname);
        free (fname);
        return -1;
    }

    scarletbook_area_t *area = &info->handle->area[area_idx];
    if (!area->area_toc || !area->area_tracklist_offset) {
        trace ("sacd_iso: corrupted area TOC in %s\n", fname);
        free (fname);
        return -1;
    }
    if (track_idx < 0 || track_idx >= area->area_toc->track_count) {
        trace ("sacd_iso: track %d out of range in %s\n", track_idx, fname);
        free (fname);
        return -1;
    }
    info->is_dst = (area->area_toc->frame_format == FRAME_FORMAT_DST);
    free (fname);

    info->area_idx = area_idx;
    info->channels = area->area_toc->channel_count;
    info->start_lsn = area->area_tracklist_offset->track_start_lsn[track_idx];
    info->length_lsn = area->area_tracklist_offset->track_length_lsn[track_idx];
    info->current_lsn = info->start_lsn;
    info->end_lsn = info->start_lsn + info->length_lsn;
    info->duration_sec = track_duration_sec (area, track_idx);

    info->read_buffer = malloc (SECTORS_PER_READ * SACD_LSN_SIZE);
    info->d2p = dsd2pcm_create (info->channels, DECIM);
    if (!info->read_buffer || !info->d2p) {
        trace ("sacd_iso: out of memory\n");
        return -1;
    }

    if (info->is_dst) {
        info->dst_dec = dst_decoder_create (info->channels);
        info->dst_dsd_size = FRAME_SIZE_64 * info->channels;
        info->dst_dsd_buffer = malloc (info->dst_dsd_size);
        if (!info->dst_dec || !info->dst_dsd_buffer) {
            trace ("sacd_iso: failed to init DST decoder\n");
            return -1;
        }
    }

    scarletbook_frame_init (info->handle);

    _info->plugin = &plugin;
    _info->fmt.bps = 32;
    _info->fmt.is_float = 1;
    _info->fmt.channels = info->channels;
    _info->fmt.samplerate = PCM_RATE;
    for (int i = 0; i < info->channels; i++) {
        _info->fmt.channelmask |= 1 << i;
    }
    _info->readpos = 0;
    return 0;
}

static void
sacd_dec_free (DB_fileinfo_t *_info) {
    sacd_fileinfo_t *info = (sacd_fileinfo_t *)_info;
    if (!info) {
        return;
    }
    if (info->handle) {
        scarletbook_close (info->handle);
    }
    if (info->sacd) {
        sacd_close (info->sacd);
    }
    dst_decoder_destroy (info->dst_dec);
    free (info->dst_dsd_buffer);
    dsd2pcm_destroy (info->d2p);
    free (info->read_buffer);
    free (info->dsd_queue);
    free (info->pcm_buffer);
    free (info);
}

static int
sacd_dec_read (DB_fileinfo_t *_info, char *buffer, int nbytes) {
    sacd_fileinfo_t *info = (sacd_fileinfo_t *)_info;
    int frame_bytes = info->channels * (int)sizeof (float);
    int want_frames = nbytes / frame_bytes;
    int done = 0;

    while (done < want_frames) {
        if (info->pcm_pos >= info->pcm_frames) {
            info->pcm_frames = 0;
            info->pcm_pos = 0;
            if (fill (info) < 0) {
                info->eof = 1;
                break;
            }
            if (info->skip_frames > 0) {
                size_t skip = (size_t)info->skip_frames < info->pcm_frames
                    ? (size_t)info->skip_frames : info->pcm_frames;
                info->pcm_pos = skip;
                info->skip_frames -= skip;
            }
            if (info->pcm_pos >= info->pcm_frames) {
                continue; // decimator warmup, get more data
            }
        }
        size_t avail = info->pcm_frames - info->pcm_pos;
        size_t n = avail < (size_t)(want_frames - done) ? avail : (size_t)(want_frames - done);
        memcpy (buffer + (size_t)done * frame_bytes,
                info->pcm_buffer + info->pcm_pos * info->channels,
                n * frame_bytes);
        info->pcm_pos += n;
        done += n;
    }

    info->frames_played += done;
    _info->readpos = (float)((double)info->frames_played / PCM_RATE);
    return done * frame_bytes;
}

static int
sacd_dec_seek_sample (DB_fileinfo_t *_info, int sample) {
    sacd_fileinfo_t *info = (sacd_fileinfo_t *)_info;
    if (sample < 0) {
        sample = 0;
    }

    if (info->is_dst) {
        // DST is variable bitrate: sector positions are not linear in time,
        // so seek proportionally and let the frame parser resync.
        int64_t total = (int64_t)(info->duration_sec * PCM_RATE);
        if ((int64_t)sample > total) {
            sample = (int)total;
        }
        double frac = total > 0 ? (double)sample / total : 0.0;
        info->current_lsn = info->start_lsn + (uint32_t)(frac * info->length_lsn);
        info->skip_frames = 0;
        // reinit the DST decoder to drop any stale frame state
        if (info->dst_dec) {
            dst_decoder_destroy (info->dst_dec);
            info->dst_dec = dst_decoder_create (info->channels);
        }
    }
    else {
        // DSD is constant bitrate: one sector of audio carries FRAME_SIZE_64
        // DSD bytes per channel, i.e. FRAME_SIZE_64*8/DECIM PCM frames
        int64_t pcm_per_sector = FRAME_SIZE_64 * 8 / DECIM;
        int64_t max_sample = (int64_t)info->length_lsn * pcm_per_sector;
        if (sample > max_sample) {
            sample = max_sample;
        }

        uint32_t lsn_off = (uint32_t)(sample / pcm_per_sector);

        info->current_lsn = info->start_lsn + lsn_off;
        info->skip_frames = sample % pcm_per_sector;
    }

    info->dsd_queue_size = 0;
    info->pcm_frames = 0;
    info->pcm_pos = 0;
    info->frames_played = sample - info->skip_frames;
    info->eof = 0;

    dsd2pcm_reset (info->d2p);
    scarletbook_frame_init (info->handle);

    _info->readpos = (float)((double)sample / PCM_RATE);
    return 0;
}

static int
sacd_dec_seek (DB_fileinfo_t *_info, float seconds) {
    if (seconds < 0) {
        seconds = 0;
    }
    return sacd_dec_seek_sample (_info, (int)(seconds * PCM_RATE));
}

static DB_playItem_t *
sacd_dec_insert (ddb_playlist_t *plt, DB_playItem_t *after, const char *fname) {
    sacd_reader_t *sacd = sacd_open (fname);
    if (!sacd) {
        return NULL;
    }

    scarletbook_handle_t *handle = scarletbook_open (sacd);
    if (!handle) {
        sacd_close (sacd);
        return NULL; // not a SACD image
    }

    int area_idx = pick_area (handle);
    if (area_idx < 0) {
        trace ("sacd_iso: no audio areas found in %s\n", fname);
        scarletbook_close (handle);
        sacd_close (sacd);
        return NULL;
    }

    scarletbook_area_t *area = &handle->area[area_idx];
    if (!area->area_toc || !area->area_tracklist_offset) {
        trace ("sacd_iso: corrupted area TOC in %s\n", fname);
        scarletbook_close (handle);
        sacd_close (sacd);
        return NULL;
    }

    int count = area->area_toc->track_count;
    const char *album = handle->master_text.album_title;
    const char *album_artist = handle->master_text.album_artist;

    for (int i = 0; i < count; i++) {
        DB_playItem_t *it = deadbeef->pl_item_alloc_init (fname, plugin.plugin.id);
        if (!it) {
            break;
        }

        deadbeef->pl_set_meta_int (it, ":TRACKNUM", i);
        deadbeef->pl_set_meta_int (it, ":SACD_AREA", area_idx);

        char trk[10];
        snprintf (trk, sizeof (trk), "%d", i + 1);
        deadbeef->pl_add_meta (it, "track", trk);

        const char *title = area->area_track_text[i].track_type_title;
        if (title && *title) {
            deadbeef->pl_add_meta (it, "title", title);
        }
        const char *performer = area->area_track_text[i].track_type_performer;
        if (performer && *performer) {
            deadbeef->pl_add_meta (it, "artist", performer);
        }
        else if (album_artist && *album_artist) {
            deadbeef->pl_add_meta (it, "artist", album_artist);
        }
        if (album && *album) {
            deadbeef->pl_add_meta (it, "album", album);
        }

        deadbeef->pl_add_meta (it, ":FILETYPE", "SACD");
        deadbeef->plt_set_item_duration (plt, it, track_duration_sec (area, i));

        if (count > 1) {
            deadbeef->pl_set_item_flags (it, deadbeef->pl_get_item_flags (it) | DDB_IS_SUBTRACK);
        }

        after = deadbeef->plt_insert_item (plt, after, it);
        deadbeef->pl_item_unref (it);
    }

    scarletbook_close (handle);
    sacd_close (sacd);
    return after;
}

static int
sacd_dec_read_metadata (DB_playItem_t *it) {
    (void)it; // all metadata is added in insert()
    return 0;
}

static const char *exts[] = { "iso", NULL };

static DB_decoder_t plugin = {
    DDB_PLUGIN_SET_API_VERSION
    .plugin.type = DB_PLUGIN_DECODER,
    .plugin.id = "sacd_iso",
    .plugin.name = "SACD ISO player",
    .plugin.descr = "Plays SACD ISO images (DSD), converting DSD to 88.2 kHz PCM",
    .plugin.version_major = 0,
    .plugin.version_minor = 1,
    .open = sacd_dec_open,
    .init = sacd_dec_init,
    .free = sacd_dec_free,
    .read = sacd_dec_read,
    .seek = sacd_dec_seek,
    .seek_sample = sacd_dec_seek_sample,
    .insert = sacd_dec_insert,
    .read_metadata = sacd_dec_read_metadata,
    .exts = exts,
};

DB_plugin_t *
sacd_iso_load (DB_functions_t *api) {
    deadbeef = api;
    return DB_PLUGIN (&plugin);
}

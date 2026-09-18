#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "scarletbook.h"
#include "scarletbook_helpers.h"

const char *character_set[] = {
    "UNKNOWN", "ISO646", "ISO8859-1", "RIS506",
    "KSC5601", "GB2312", "BIG5", "ISO8859-1-ESC"
};

const char *album_genre[] = {
    "Not Used", "Not Defined", "Adult Contemporary", "Alternative Rock",
    "Children's Music", "Classical", "Contemporary Christian", "Country",
    "Dance", "Easy Listening", "Erotic", "Folk", "Gospel", "Hip Hop",
    "Jazz", "Latin", "Musical", "New Age", "Opera", "Operetta",
    "Pop Music", "Rap", "Reggae", "Rock Music", "Rhythm and Blues",
    "Sound Effects", "Sound Track", "Spoken Word", "World Music", "Blues"
};

const char *album_category[] = { "Not Used", "General", "Japanese" };

int has_two_channel(scarletbook_handle_t *handle) {
    return handle && handle->twoch_area_idx >= 0;
}

int has_multi_channel(scarletbook_handle_t *handle) {
    return handle && handle->mulch_area_idx >= 0;
}

static char *strdup_safe(const char *s) {
    if (!s) return NULL;
    char *d = malloc(strlen(s) + 1);
    if (d) strcpy(d, s);
    return d;
}

static void sanitize_filename(char *s) {
    if (!s) return;
    for (char *p = s; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' ||
            *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|')
            *p = '_';
    }
}

char *get_speaker_config_string(area_toc_t *area_toc) {
    if (!area_toc) return strdup_safe("Unknown");
    if (area_toc->channel_count == 2 && area_toc->loudspeaker_config == 0)
        return strdup_safe("Stereo");
    if (area_toc->channel_count == 6)
        return strdup_safe("MultiChannel");
    if (area_toc->channel_count == 5)
        return strdup_safe("MultiChannel_5.0");
    return strdup_safe("MultiChannel_Other");
}

char *get_path_disc_album(scarletbook_handle_t *handle, int artist_flag) {
    if (!handle) return NULL;
    const char *album = handle->master_text.album_title;
    const char *artist = handle->master_text.album_artist;
    if (!album) album = "Unknown_Album";
    if (artist_flag && artist && *artist) {
        size_t len = strlen(artist) + 1 + strlen(album) + 1;
        char *path = malloc(len);
        snprintf(path, len, "%s/%s", artist, album);
        sanitize_filename(path);
        return path;
    }
    char *path = strdup_safe(album);
    sanitize_filename(path);
    return path;
}

char *get_album_dir(scarletbook_handle_t *handle, int artist_flag) {
    return get_path_disc_album(handle, artist_flag);
}

char *get_music_filename(scarletbook_handle_t *handle, int area_idx, int track_idx, const char *conc_string, int performer_flag) {
    if (!handle || area_idx < 0 || area_idx >= 4) return NULL;
    scarletbook_area_t *area = &handle->area[area_idx];
    if (track_idx < 0 || track_idx >= area->area_toc->track_count) return NULL;

    char *title = area->area_track_text[track_idx].track_type_title;
    char *performer = area->area_track_text[track_idx].track_type_performer;
    if (!title) title = "Unknown_Track";

    char track_num[8];
    snprintf(track_num, sizeof(track_num), "%02d", track_idx + 1);

    size_t len = strlen(track_num) + 3 + strlen(title) + 1;
    if (conc_string) len += strlen(conc_string) + 1;
    if (performer_flag && performer) len += 3 + strlen(performer);

    char *filename = malloc(len);
    if (!filename) return NULL;

    if (conc_string) {
        snprintf(filename, len, "%s %s %s", track_num, conc_string, title);
    } else {
        snprintf(filename, len, "%s %s", track_num, title);
    }
    if (performer_flag && performer) {
        strcat(filename, " - ");
        strcat(filename, performer);
    }
    sanitize_filename(filename);
    return filename;
}
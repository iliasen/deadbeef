#ifndef SCARLETBOOK_HELPERS_H_INCLUDED
#define SCARLETBOOK_HELPERS_H_INCLUDED

#include "scarletbook.h"

#ifdef __cplusplus
extern "C" {
#endif

int has_two_channel(scarletbook_handle_t *handle);
int has_multi_channel(scarletbook_handle_t *handle);
char *get_album_dir(scarletbook_handle_t *handle, int artist_flag);
char *get_music_filename(scarletbook_handle_t *handle, int area_idx, int track_idx, const char *conc_string, int performer_flag);
char *get_path_disc_album(scarletbook_handle_t *handle, int artist_flag);
char *get_speaker_config_string(area_toc_t *area_toc);

#ifdef __cplusplus
}
#endif

#endif
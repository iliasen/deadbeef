#ifndef SCARLETBOOK_READ_H_INCLUDED
#define SCARLETBOOK_READ_H_INCLUDED

#include "scarletbook.h"

#ifdef __cplusplus
extern "C" {
#endif

void scarletbook_frame_init(scarletbook_handle_t *handle);
int scarletbook_process_frames(scarletbook_handle_t *handle, uint8_t *read_buffer, int blocks_read_in, int last_block, frame_read_callback_t frame_read_callback, void *userdata);

#ifdef __cplusplus
}
#endif

#endif
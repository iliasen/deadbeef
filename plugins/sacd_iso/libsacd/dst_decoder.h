#ifndef DST_DECODER_H_INCLUDED
#define DST_DECODER_H_INCLUDED

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dst_decoder_s dst_decoder_t;

dst_decoder_t *dst_decoder_create(int channel_count);
void dst_decoder_destroy(dst_decoder_t *dec);
int dst_decoder_decode(dst_decoder_t *dec, const uint8_t *dst_data, size_t dst_size, uint8_t *dsd_out, size_t *dsd_out_size);

#define DST_FRAME_SIZE 4704

#ifdef __cplusplus
}
#endif

#endif
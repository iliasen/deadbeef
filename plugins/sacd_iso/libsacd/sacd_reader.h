#ifndef SACD_READER_H_INCLUDED
#define SACD_READER_H_INCLUDED

#include <stdint.h>
#include "sacd_input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sacd_reader_s sacd_reader_t;

sacd_reader_t *sacd_open(const char *path);
void sacd_close(sacd_reader_t *);
uint32_t sacd_read_block_raw(sacd_reader_t *, uint32_t, uint32_t, uint8_t *);
int sacd_decrypt(sacd_reader_t *, uint8_t *, uint32_t);
int sacd_authenticate(sacd_reader_t *);
uint32_t sacd_get_total_sectors(sacd_reader_t *);

#ifdef __cplusplus
}
#endif

#endif
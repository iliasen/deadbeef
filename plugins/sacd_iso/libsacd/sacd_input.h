#ifndef SACD_INPUT_H_INCLUDED
#define SACD_INPUT_H_INCLUDED

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sacd_input_s *sacd_input_t;

sacd_input_t sacd_input_open(const char *target);
int sacd_input_close(sacd_input_t dev);
uint32_t sacd_input_read(sacd_input_t dev, uint32_t pos, uint32_t blocks, void *buffer);
char *sacd_input_error(sacd_input_t dev);
int sacd_input_authenticate(sacd_input_t dev);
int sacd_input_decrypt(sacd_input_t dev, uint8_t *buffer, uint32_t blocks);
uint32_t sacd_input_total_sectors(sacd_input_t dev);

#ifdef __cplusplus
}
#endif

#endif
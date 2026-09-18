#include <stdlib.h>
#include <string.h>
#include "sacd_reader.h"
#include "sacd_input.h"

struct sacd_reader_s {
    sacd_input_t input;
};

sacd_reader_t *sacd_open(const char *path) {
    sacd_reader_t *sacd = calloc(1, sizeof(*sacd));
    if (!sacd) return NULL;

    sacd->input = sacd_input_open(path);
    if (!sacd->input) {
        free(sacd);
        return NULL;
    }
    return sacd;
}

void sacd_close(sacd_reader_t *sacd) {
    if (!sacd) return;
    if (sacd->input) sacd_input_close(sacd->input);
    free(sacd);
}

uint32_t sacd_read_block_raw(sacd_reader_t *sacd, uint32_t pos, uint32_t blocks, uint8_t *data) {
    if (!sacd || !sacd->input) return 0;
    return sacd_input_read(sacd->input, pos, blocks, data);
}

int sacd_decrypt(sacd_reader_t *sacd, uint8_t *buffer, uint32_t blocks) {
    if (!sacd || !sacd->input) return -1;
    return sacd_input_decrypt(sacd->input, buffer, blocks);
}

int sacd_authenticate(sacd_reader_t *sacd) {
    if (!sacd || !sacd->input) return -1;
    return sacd_input_authenticate(sacd->input);
}

uint32_t sacd_get_total_sectors(sacd_reader_t *sacd) {
    if (!sacd || !sacd->input) return 0;
    return sacd_input_total_sectors(sacd->input);
}
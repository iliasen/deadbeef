#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "sacd_input.h"

#define SACD_LSN_SIZE 2048

struct sacd_input_s {
    int fd;
};

static int sacd_dev_input_authenticate(sacd_input_t dev) {
    (void)dev;
    return 0;
}

static int sacd_dev_input_decrypt(sacd_input_t dev, uint8_t *buffer, uint32_t blocks) {
    (void)dev; (void)buffer; (void)blocks;
    return 0;
}

static sacd_input_t sacd_dev_input_open(const char *target) {
    sacd_input_t dev = calloc(1, sizeof(*dev));
    if (!dev) return NULL;

#if defined(_WIN32) || defined(_WIN64)
    dev->fd = open(target, O_RDONLY | O_BINARY);
#else
    dev->fd = open(target, O_RDONLY);
#endif

    if (dev->fd < 0) {
        free(dev);
        return NULL;
    }
    return dev;
}

static char *sacd_dev_input_error(sacd_input_t dev) {
    (void)dev;
    return (char *)"unknown error";
}

static uint32_t sacd_dev_input_read(sacd_input_t dev, uint32_t pos, uint32_t blocks, void *buffer) {
    off_t ret_lseek = lseek(dev->fd, (off_t)pos * SACD_LSN_SIZE, SEEK_SET);
    if (ret_lseek < 0) return 0;

    size_t len = (size_t)blocks * SACD_LSN_SIZE;
    ssize_t ret = read(dev->fd, buffer, len);
    if (ret <= 0) return 0;
    if ((size_t)ret < len) return (uint32_t)ret / SACD_LSN_SIZE;
    return blocks;
}

static int sacd_dev_input_close(sacd_input_t dev) {
    int ret = close(dev->fd);
    free(dev);
    return ret;
}

static uint32_t sacd_dev_input_total_sectors(sacd_input_t dev) {
    if (!dev) return 0;
    struct stat file_stat;
    if (fstat(dev->fd, &file_stat) < 0) return 0;
    return (uint32_t)(file_stat.st_size / SACD_LSN_SIZE);
}

sacd_input_t (*sacd_input_open)(const char *) = sacd_dev_input_open;
int (*sacd_input_close)(sacd_input_t) = sacd_dev_input_close;
uint32_t (*sacd_input_read)(sacd_input_t, uint32_t, uint32_t, void *) = sacd_dev_input_read;
char *(*sacd_input_error)(sacd_input_t) = sacd_dev_input_error;
int (*sacd_input_authenticate)(sacd_input_t) = sacd_dev_input_authenticate;
int (*sacd_input_decrypt)(sacd_input_t, uint8_t *, uint32_t) = sacd_dev_input_decrypt;
uint32_t (*sacd_input_total_sectors)(sacd_input_t) = sacd_dev_input_total_sectors;
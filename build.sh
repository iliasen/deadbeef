#!/bin/bash

cd "$(dirname "$0")/plugins/sacd_iso" || exit 1

rm -f sacd_iso.so

echo "Компилируем плагин..."

gcc -shared -fPIC \
    -O2 -Wall \
    -I../../include \
    -Ilibsacd \
    sacd_iso.c \
    dsd2pcm.c \
    libsacd/sacd_reader.c \
    libsacd/sacd_input.c \
    libsacd/scarletbook.c \
    libsacd/scarletbook_helpers.c \
    libsacd/scarletbook_read.c \
    libsacd/dst_decoder.c \
    libsacd/dst/ccp_calc.c \
    libsacd/dst/dst_ac.c \
    libsacd/dst/dst_data.c \
    libsacd/dst/dst_fram.c \
    libsacd/dst/dst_init.c \
    libsacd/dst/unpack_dst.c \
    -lm \
    -o sacd_iso.so

if [ $? -eq 0 ]; then
    echo "Успешно! Создан файл plugins/sacd_iso/sacd_iso.so"
else
    echo "Ошибка компиляции."
fi

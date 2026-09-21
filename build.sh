#!/bin/bash

cd "$(dirname "$0")/plugins/sacd_iso" || exit 1

rm -f sacd_iso.so

echo "Компилируем плагин..."

g++ -shared -fPIC \
    -O2 -Wall \
    -std=c++17 \
    -I../../include \
    -Ilibsacd \
    -Ilibsacd/dst2 \
    sacd_iso.c \
    dsd2pcm.c \
    libsacd/sacd_reader.c \
    libsacd/sacd_input.c \
    libsacd/scarletbook.c \
    libsacd/scarletbook_helpers.c \
    libsacd/scarletbook_read.c \
    libsacd/dst_decoder.cpp \
    libsacd/dst2/decoder/decoder.cpp \
    -lm \
    -o sacd_iso.so

if [ $? -eq 0 ]; then
    echo "Успешно! Создан файл plugins/sacd_iso/sacd_iso.so"
else
    echo "Ошибка компиляции."
fi
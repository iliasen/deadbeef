#!/bin/bash

cd "$(dirname "$0")/plugins/sacd_iso" || exit 1

rm -f sacd_iso.so
mkdir -p build

echo "Компилируем плагин..."

INCLUDES="-I../../include -Ilibsacd -Ilibsacd/dst2"
CFLAGS="-O2 -Wall -fPIC -std=gnu99 $INCLUDES"
CXXFLAGS="-O2 -Wall -fPIC -std=c++17 $INCLUDES"

set -e

gcc $CFLAGS -c sacd_iso.c                      -o build/sacd_iso.o
gcc $CFLAGS -c dsd2pcm.c                       -o build/dsd2pcm.o
gcc $CFLAGS -c libsacd/sacd_reader.c           -o build/sacd_reader.o
gcc $CFLAGS -c libsacd/sacd_input.c            -o build/sacd_input.o
gcc $CFLAGS -c libsacd/scarletbook.c           -o build/scarletbook.o
gcc $CFLAGS -c libsacd/scarletbook_helpers.c   -o build/scarletbook_helpers.o
gcc $CFLAGS -c libsacd/scarletbook_read.c      -o build/scarletbook_read.o

gcc $CFLAGS -c libsacd/dst_ff.c                -o build/dst_ff.o

# Link
gcc -shared -fPIC build/*.o -lm -o sacd_iso.so

set +e

if [ $? -eq 0 ]; then
    echo "Успешно! Создан файл plugins/sacd_iso/sacd_iso.so"
else
    echo "Ошибка компиляции."
fi

#!/bin/bash
set -e

MOUNT_POINT="/mnt/vt"
SOURCE_FILE="/tmp/test_data_source.bin"
TMP_FILE2="/tmp/test_data2.bin"

BLOCK_SIZES=(512 1024 4096)
BLOCK_COUNTS=(1 10 100)

echo "=== VTFS TESTS ==="

# 1. dd из /dev/random куда-нибудь в tmp файл за пределы вашей фс
echo "Создание тестового файла из /dev/random..."
dd if=/dev/random of="$SOURCE_FILE" bs=4096 count=100 status=progress

echo "Тестирование с разными размерами блоков и количеством..."
for bs in "${BLOCK_SIZES[@]}"; do
    for count in "${BLOCK_COUNTS[@]}"; do
        echo "Тест: bs=$bs count=$count"
        # 2. dd из tmp файла в файл в вашей фс.
        dd if="$SOURCE_FILE" of="$MOUNT_POINT/test_${bs}_${count}.bin" bs=$bs count=$count status=none
        # 3. dd из файла вашей фс в tmp2 файл за пределами вашей фс
        dd if="$MOUNT_POINT/test_${bs}_${count}.bin" of="$TMP_FILE2" bs=$bs count=$count status=none
        # 4. diff tmp tmp2 должен показать, что файлы идентичные
        # (сравниваем с исходным файлом, обрезанным до нужного размера)
        dd if="$SOURCE_FILE" of="/tmp/expected_${bs}_${count}.bin" bs=$bs count=$count status=none
        if diff -q "/tmp/expected_${bs}_${count}.bin" "$TMP_FILE2" > /dev/null; then
            echo "[OK] Файлы идентичны"
        else
            echo "[ERROR] Файлы различаются!"
            rm -f "/tmp/expected_${bs}_${count}.bin"
            exit 1
        fi
        rm -f "/tmp/expected_${bs}_${count}.bin"
    done
done

rm -f "$SOURCE_FILE" "$TMP_FILE2"
echo "=== VTFS TESTS FINISH ==="
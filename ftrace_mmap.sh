#!/bin/bash

set -e

TRACE_DIR="/sys/kernel/debug/tracing"
TEST_FILE="/tmp/test_mmap_file.bin"
TEST_SOURCE="test_mmap_external.c"
TEST_BINARY="test_mmap_external"

echo "=== Настройка ftrace для отслеживания mmap ==="

# Компиляция тестовой программы
if [ -f "$TEST_SOURCE" ]; then
    echo "Компиляция тестовой программы..."
    gcc -o "$TEST_BINARY" "$TEST_SOURCE" -Wall -Wextra
    if [ $? -ne 0 ]; then
        echo "[ERROR] Ошибка компиляции тестовой программы"
        exit 1
    fi
    echo "[OK] Тестовая программа скомпилирована"
fi

# Проверка прав root
if [ "$EUID" -ne 0 ]; then 
    echo "[ERROR] Скрипт должен запускаться от root"
    exit 1
fi

# Проверка доступности ftrace
if [ ! -d "$TRACE_DIR" ]; then
    echo "[ERROR] ftrace не доступен"
    exit 1
fi

# Создать тестовый файл
echo "Создание тестового файла: $TEST_FILE"
echo "Test data for mmap" > "$TEST_FILE"
dd if=/dev/random of="$TEST_FILE" bs=4096 count=10 2>/dev/null || true

# Выключить ftrace если был включен
echo 0 > "$TRACE_DIR/tracing_on" 2>/dev/null || true

# Очистить предыдущие настройки
echo nop > "$TRACE_DIR/current_tracer"
echo > "$TRACE_DIR/set_ftrace_filter"
echo > "$TRACE_DIR/set_graph_function"
echo > "$TRACE_DIR/trace"

# Установить function_graph tracer
echo "Установка function_graph tracer..."
echo function_graph > "$TRACE_DIR/current_tracer"

# Настроить детальную трассировку
echo "Настройка детальной трассировки __arm64_sys_mmap..."
echo "__arm64_sys_mmap" > "$TRACE_DIR/set_graph_function"

# Настроить глубину стека для детального анализа
echo 15 > "$TRACE_DIR/max_graph_depth"

# Очистить trace buffer
echo > "$TRACE_DIR/trace"

# Включить трассировку
echo 1 > "$TRACE_DIR/tracing_on"

echo ""
echo "=== Трассировка включена ==="
if [ -f "$TEST_BINARY" ]; then
    ./"$TEST_BINARY" &
    TEST_PID=$!
    echo "PID тестовой программы: $TEST_PID"
    
    # Установить фильтр по PID
    echo $TEST_PID > "$TRACE_DIR/set_ftrace_pid" 2>/dev/null || true
    
    wait $TEST_PID
else
    echo "[ERROR] Тестовая программа $TEST_BINARY не найдена"
fi

# Выключить трассировку
echo 0 > "$TRACE_DIR/tracing_on"

# Сохранить результаты
OUTPUT_FILE="mmap_stacktrace.txt"
echo "Сохранение результатов в $OUTPUT_FILE..."
cat "$TRACE_DIR/trace" > "$OUTPUT_FILE"

echo ""
echo "=== Полный стектрейс сохранен в $OUTPUT_FILE ==="

# Очистка
rm -f "$TEST_FILE"
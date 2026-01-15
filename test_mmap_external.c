#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define TEST_FILE "/tmp/test_mmap_file.bin"

int main(void) {
    printf("=== Тест mmap на файл вне VTFS ===\n\n");
    
    // Open file
    int fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    // Get size
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return 1;
    }
    
    size_t file_size = st.st_size;
    printf("Размер файла: %zu байт\n", file_size);
    
    // Call mmap
    printf("Вызов mmap()...\n");
    char *mapped = mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
    
    if (mapped == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }
    
    printf("Файл отображен по адресу: %p\n", mapped);
    
    sleep(1);

    // Unmap
    printf("Освобождение отображения...\n");
    munmap(mapped, file_size);
    close(fd);
    
    printf("Тест завершен успешно\n");
    return 0;
}

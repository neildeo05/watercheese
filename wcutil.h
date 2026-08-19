#ifndef UTIL_H_
#define UTIL_H_
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "watercheese_types.h"

uint64_t read_payload(const char *path, uint8_t **code_out)
{
    if (path == NULL || code_out == NULL) {
        errno = EINVAL;
        return 0;
    }

    *code_out = NULL;

    const uint64_t total_size =
        (uint64_t)PAYLOAD_SIZE + (uint64_t)STACK_SIZE;

    if (total_size > SIZE_MAX) {
        errno = EOVERFLOW;
        return 0;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }

    struct stat file_stat;
    if (fstat(fileno(file), &file_stat) == -1) {
        const int error = errno;
        fclose(file);
        errno = error;
        return 0;
    }

    if (file_stat.st_size < 0 ||
        (uint64_t)file_stat.st_size != total_size) {
        fclose(file);
        errno = EINVAL;
        return 0;
    }

    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        fclose(file);
        errno = EINVAL;
        return 0;
    }

    uint8_t *code = NULL;
    const int allocation_error = posix_memalign(
        (void **)&code,
        (size_t)page_size,
        (size_t)total_size
    );

    if (allocation_error != 0) {
        fclose(file);
        errno = allocation_error;
        return 0;
    }

    const size_t bytes_read = fread(
        code,
        1,
        (size_t)total_size,
        file
    );

    if (bytes_read != (size_t)total_size) {
        const int error = ferror(file) ? errno : EIO;

        fclose(file);
        free(code);

        errno = error;
        return 0;
    }

    if (fclose(file) == EOF) {
        const int error = errno;

        free(code);
        errno = error;
        return 0;
    }

    *code_out = code;
    return total_size;
}
#endif
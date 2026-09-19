#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#define DEBUG(fmt, ...)                                                                            \
    do {                                                                                           \
        fprintf(stderr, "[debug] ");                                                               \
        fprintf(stderr, fmt, ##__VA_ARGS__);                                                       \
    } while (false)

uint64_t byte_count = 0;
bool debug = false;

int process_input(int file_desc)
{
    while (true) {
        // read data from file descriptor
        uint8_t buf[4096];
        ssize_t n = read(file_desc, buf, sizeof buf);

        if (n == -1) {
            // read returned an error
            switch (errno) {
                case EINTR:
                    continue;
                default:
                    perror("read");
                    return -1;
            }
        } else if (n == 0) {
            // reached EOF
            break;
        }

        for (ssize_t i = 0; i < n; ++i) {
            uint8_t byte = buf[i];
            /* process_byte(byte); */
            DEBUG("processing byte: %u", byte);
            byte_count++;
        }

        // loop through data byte-by-byte
        // call process_byte(byte);
    }

    return 0;
}

void print_hash() {}

int main(int argc, char **argv)
{
    debug = getenv("DEBUG") != NULL;
    int file_desc = 0;

    // read file arg
    if (argc > 1) {
        char *fname = argv[1];

        DEBUG("opening file: %s\n", fname);
        file_desc = open(fname, O_RDONLY);

        if (file_desc == -1) {
            perror("open");
            return 1;
        }
    }

    process_input(file_desc);
    close(file_desc);
    print_hash();

    return 0;
}

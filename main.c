#include <fcntl.h>
#include <stdio.h>

void process_input(int file_desc) {}

void print_hash() {}

int main(int argc, char **argv)
{
    int file_desc = 0;

    // read file arg
    if (argc > 0) {
        char *fname = argv[1];

        file_desc = open(fname, O_RDONLY);

        if (file_desc == -1) {
            perror("open");
            return 1;
        }
    }

    // file is ready for reading
    process_input(file_desc);
    print_hash();

    return 0;
}

##
# C MD5
#
# @file
# @version 0.1

CC     := clang
CFLAGS := -std=c17 -Wall -Wextra -Wconversion -Wshadow -g
TARGET := md5

$(TARGET): main.c
	$(CC) $(CFLAGS) main.c -o $(TARGET)

.PHONY: clean run

run: $(TARGET)
	./$(TARGET) random_data.bin

clean:
	rm -f $(TARGET)

# end

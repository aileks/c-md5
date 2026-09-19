##
# C MD5
#
# @file
# @version 0.1

CC     := clang
CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -g
TARGET := md5

$(TARGET): main.c
	$(CC) $(CFLAGS) main.c -o $(TARGET)

.PHONY: clean run

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

# end

CC = gcc
CFLAGS = -Wall -Wextra -Iinclude
SRC = src/main.c src/process_manager.c src/disk_analyzer.c src/log_analyzer.c src/permission_checker.c
TARGET = devassist
all: $(TARGET)
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)
clean: 
	rm -f $(TARGET) 
.PHONY:all clean


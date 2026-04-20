# 编译器及编译选项
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -Iinclude
# 需要链接多线程库和 sqlite3 库
LDFLAGS = -lpthread -lsqlite3

# 目标文件及可执行文件名
TARGET = webssh_server
SRCS = src/server.c src/file_io.c src/db_helper.c src/cmd_validator.c src/sys_monitor.c
OBJS = $(SRCS:.c=.o)

# 默认一键编译目标
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理目标
clean:
	rm -f $(OBJS) $(TARGET) webssh_log.db

.PHONY: all clean
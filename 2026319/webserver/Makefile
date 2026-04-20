CC = gcc
CFLAGS = -Wall -g -Iinclude
SRCS = src/main.c src/request.c src/response.c src/server.c src/util.c
OBJS = $(SRCS:.c=.o)
TARGET = webserver

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lsqlite3 -lpthread

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean

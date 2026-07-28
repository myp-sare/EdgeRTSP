CC = gcc
CFLAGS = -Iinclude
TARGET = build/rtsp_server
SRCS = src/main.c src/rtsp_server.c src/utils.c src/rtp_sender.c

all: $(TARGET)

$(TARGET): $(SRCS)
		$(CC) -o $@ $^ $(CFLAGS)

clean:
		rm -f $(TARGET)

run: $(TARGET)
		./$(TARGET)
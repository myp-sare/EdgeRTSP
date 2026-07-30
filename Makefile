CC = gcc
CFLAGS = -Iinclude
TARGET = build/rtsp_server
SRCS = src/main.c src/rtsp_server.c src/utils.c src/rtp_sender.c

all: $(TARGET)

$(TARGET): $(SRCS)
		$(CC) -o $@ $^ $(CFLAGS)

# =========== 封库==========
LIB = build/librtpsender.a
LIB_OBJS = build/obj/rtp_sender.o build/obj/rtsp_server.o build/obj/utils.o

lib: $(LIB)

$(LIB): $(LIB_OBJS)
		ar rcs $@ $^

build/obj/rtp_sender.o: src/rtp_sender.c
		@mkdir -p build/obj
		$(CC) $(CFLAGS) -c $< -o $@

build/obj/rtsp_server.o: src/rtsp_server.c
		@mkdir -p build/obj
		$(CC) $(CFLAGS) -c $< -o $@

build/obj/utils.o: src/utils.c
		@mkdir -p build/obj
		$(CC) $(CFLAGS) -c $< -o $@
#==========================

clean:
		rm -f $(TARGET) $(LIB) build/obj/*.o

run: $(TARGET)
		./$(TARGET)
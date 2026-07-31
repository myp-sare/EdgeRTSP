CC = gcc
CFLAGS = -Iinclude
TARGET = build/rtsp_server
SRCS = src/main.c src/rtsp_server.c src/utils.c src/rtp_sender.c
OBJS    = $(SRCS:src/%=build/obj/%)
OBJS    := $(OBJS:.c=.o)
LIB = build/librtpsender.a
LIB_OBJS = build/obj/rtp_sender.o build/obj/rtsp_server.o build/obj/utils.o

all: $(TARGET)

$(TARGET): $(OBJS)
		$(CC) -o $@ $(OBJS) $(CFLAGS)

# =========== 封库==========
lib: $(LIB)

$(LIB): $(LIB_OBJS)
		ar rcs $@ $^

build/obj/%.o: src/%.c
	@mkdir -p build/obj
	$(CC) $(CFLAGS) -c $< -o $@
#==========================

run: $(TARGET)
		./$(TARGET)

clean:
		rm -f $(TARGET) $(LIB) $(OBJS)
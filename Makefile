CC ?= gcc
INC_PATH ?= $(realpath ../inc)
LIB_PATH ?= $(realpath ../lib)
LIBS ?= $(wildcard $(LIB_PATH)/*.a) -pthread -lrt -lm
LDFLAGS :=-g -L$(LIB_PATH)
CFLAGS +=-g -I$(INC_PATH)

EXAMPLES=v2xcast_sdk_socket

.PHONY: all

all: $(EXAMPLES)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

v2xcast_sdk_socket: v2xcast_sdk_socket.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS) -Wl,-rpath,'$$ORIGIN/../../lib'

clean:
	rm -f *~ *.o $(EXAMPLES)

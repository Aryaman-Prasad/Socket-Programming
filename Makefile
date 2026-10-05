GCC=gcc
CFLAGS=

BIN=server client
SRCS=server.c client.c

all: $(BIN)

%: %.c
	$(GCC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(BIN)

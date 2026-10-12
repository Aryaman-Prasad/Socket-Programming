GCC=g++
CFLAGS=

BIN=server client
SRCS=server.cpp client.cpp

all: $(BIN)

%: %.c
	$(GCC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(BIN)

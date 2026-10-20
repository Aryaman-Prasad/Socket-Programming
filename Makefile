GCC=gcc
GXX=g++
CFLAGS=
CXXFLAGS=

BIN=server client

all: $(BIN)

server: server.c
	$(GCC) $(CFLAGS) -o $@ $<

client: client.cpp
	$(GXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(BIN)

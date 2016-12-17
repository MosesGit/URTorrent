CFLAGS ?= -g++
CFLAGS += -Wall
CPPFLAGS += -DBE_DEBUG
CPPFLAGS += -lcrypto

all: test attch url urtorrent

test: urtorrent.o

attch: bencode.o

url: urlcode.o

urtorrent : urtorrent.o bencode.o
	${CFLAGS} urtorrent.o bencode.o urlcode.o ${CPPFLAGS} -o urtorrent -lpthread

clean:
	rm -f *.o core test

.PHONY: all clean

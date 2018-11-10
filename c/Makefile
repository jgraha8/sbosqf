CC:=gcc
CFLAGS:=-Wall -O0 -g -I/usr/local/include
LDFLAGS:=-L/usr/local/lib
LIBS:=-lbds

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

sbopkg-dep2sqf: pkglist.o deps.o user_config.o filesystem.o response.o file_mmap.o main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

.PHONY: clean

clean:
	rm -f *.o sbopkg-dep2sqf

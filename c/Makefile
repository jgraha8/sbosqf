CC:=gcc
CFLAGS:=-Wall -O0 -g -I/usr/local/include
LDFLAGS:=-L/usr/local/lib
LIBS:=-lbds

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

sbopkg-dep2sqf: pkglist.o depfile.o main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

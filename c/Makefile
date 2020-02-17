CC:=gcc
ZLIB:=${HOME}/opt/zlib/1.2.11
CFLAGS:=-Wall -Wfatal-errors -Werror -O0 -g -I. -I$(ZLIB)/include -I/mnt/shared/include -fPIC 
LDFLAGS:=-L$(ZLIB)/lib -L/mnt/shared/lib64 -Wl,-rpath,$(ZLIB)/lib
LIBS:=-lbds -lz -lm

all: sbopkg-dep2sqf

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

sbopkg-dep2sqf: c-ctype.o filevercmp.o mesg.o ostream.o string_list.o file_mmap.o sbo.o slack_pkg.o user_config.o pkg_util.o pkg.o pkg_graph.o pkg_ops.o main.o 
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

#test_pkg: c-ctype.o filevercmp.o file_mmap.o sbo.o slack_pkg.o user_config.o pkg_util.o pkg.o pkg_graph.o pkg_ops.o test_pkg.o
#	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

.PHONY: clean

clean:
	rm -f *.o test_pkg

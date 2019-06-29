CC:=gcc
CFLAGS:=-Wall -Wfatal-errors -Werror -O0 -g -I/usr/local/include
LDFLAGS:=-L/usr/local/lib 
LIBS:=-lbds -lz #-lprofiler

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

# sbopkg-dep2sqf: pkg_db.o deps.o user_config.o filesystem.o input.o file_mmap.o msg_string.o main.o
# 	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)


sbopkg-dep2sqf: file_mmap.o sbo.o slack_pkg.o user_config.o pkg_util.o pkg.o pkg_graph.o pkg_ops.o main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

test_pkg: file_mmap.o sbo.o slack_pkg.o user_config.o pkg_util.o pkg.o pkg_graph.o pkg_ops.o test_pkg.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

.PHONY: clean

clean:
	rm -f *.o test_pkg

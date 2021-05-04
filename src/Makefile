CC:=gcc
#ZLIB:=${HOME}/opt/zlib/1.2.11
#CFLAGS:=-Wall -Werror -O0 -g -I. -I$(ZLIB)/include -I/mnt/shared/include -fPIC
#LDFLAGS:=-L$(ZLIB)/lib -L/mnt/shared/lib64 -Wl,-rpath,$(ZLIB)/lib
CFLAGS:=-Wall -Werror -O0 -g -I. -fPIC
CFLAGS += $(shell pkg-config --cflags zlib)

LIBS:=-lbds -lm
LIBS += $(shell pkg-config --libs zlib)

SRC := \
	c-ctype.c \
	filevercmp.c \
	mesg.c \
	ostream.c \
	string_list.c \
	file_mmap.c \
	packages_db.c \
	sbo.c \
	slack_pkg.c \
	slack_pkg_dbi.c \
	slackpkg_repo.c \
	user_config.c \
	pkg_util.c \
	pkg.c \
	pkg_graph.c \
	pkg_io.c \
	pkg_ops.c \
	build.c \
	check_updates.c \
	edit.c \
	info.c \
	make_meta.c \
	options.c \
	output_path.c \
	remove.c \
	review.c \
	search.c \
	update.c \
	updatedb.c \
	main.c

OBJ := $(SRC:.c=.o)

all: sbosqf

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

sbosqf: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

#test_pkg: c-ctype.o filevercmp.o file_mmap.o sbo.o slack_pkg.o user_config.o pkg_util.o pkg.o pkg_graph.o pkg_ops.o test_pkg.o
#	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

.PHONY: clean

clean:
	rm -f $(OBJ) sbosqf

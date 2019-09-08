/*
 * Copyright (C) 2019 Jason Graham <jgraham@compukix.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 *
 */

#ifndef __OSTREAM_H__
#define __OSTREAM_H__

#include <stdio.h>
#include <stdbool.h>

#include <libbds/bds_vector.h>

struct ostream
{
        FILE *fp;
        struct bds_vector *output_buffer;
        bool console_stream;
};

struct ostream *ostream_open(const char *path, const char *mode, bool buffered);

bool ostream_is_console_stream(const struct ostream *os);

int ostream_printf(struct ostream *os, const char *fmt, ...);

void ostream_clear(struct ostream *os);

void ostream_close(struct ostream *os);

#endif

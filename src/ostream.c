#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <libbds/bds_string.h>

#include "ostream.h"

#define BUF_SIZE 4096

struct ostream *ostream_open(const char *path, const char *mode, bool buffer_stream)
{
	struct ostream *os = malloc(sizeof(*os));
	memset(os, 0, sizeof(*os));

	if( strcmp(path, "-") == 0 ||
	    strcmp(path, "stdout") == 0	) {
		path = "/dev/stdout";
	} else if( strcmp(path, "stderr") == 0 ) {
		path = "/dev/stderr";
	}

	os->fp = fopen(path, mode);
	if( os->fp == NULL ) {
		free(os);
		return NULL;
	}

	if( buffer_stream ) {
		// Disable the file stream buffer
		assert(0 == setvbuf(os->fp, NULL, _IONBF, 0));
		os->output_buffer = bds_vector_alloc(1, sizeof(char), NULL);
	}

	return os;
}

int ostream_printf(struct ostream *os, const char *fmt, ...)
{
	static char buf[BUF_SIZE] = {0};
	int rc = 0;

	va_list va;

	if( os->output_buffer ) {
		va_start(va, fmt);
		rc = vsnprintf(buf, BUF_SIZE-1, fmt, va);
		va_end(va);

		const size_t prev_size = bds_vector_size(os->output_buffer);
		const size_t buf_size = strlen(buf);
		const size_t size = prev_size + buf_size;

		bds_vector_resize(os->output_buffer, size);
		memcpy(bds_vector_get(os->output_buffer, prev_size), &buf[0], buf_size);

		return rc;
	}

	va_start(va, fmt);
	rc = vfprintf(os->fp, fmt, va);
	va_end(va);

	return rc;
}

void ostream_clear(struct ostream *os)
{
	if( os->output_buffer ) {
		bds_vector_clear(os->output_buffer);
	}
}

void ostream_close(struct ostream *os)
{
	if( os->output_buffer) {
		// Append null char
		char null_char = '\0';
		bds_vector_append(os->output_buffer, &null_char);

		fprintf(os->fp, "%s", (char *)bds_vector_ptr(os->output_buffer));
		bds_vector_free(&os->output_buffer);
	}
	fclose(os->fp);
	free(os);
}

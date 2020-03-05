#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mesg.h"

#define RED "\x1B[31m"
#define GREEN "\x1B[32m"
#define YELLOW "\x1B[33m"
#define BLUE "\x1B[34m"
#define MAGENTA "\x1B[35m"
#define CYAN "\x1B[36m"
#define WHITE "\x1B[37m"

#define COLOR_OK GREEN
#define COLOR_INFO YELLOW
#define COLOR_WARN MAGENTA
#define COLOR_FAIL RED
#define COLOR_END "\x1B[0m"


static void write_mesg(FILE *stream, const char *color, const char *mesg_type, const char *mesg_fmt, va_list ap ) {
	
	static char fmt[4096] = {0};

	snprintf(fmt, sizeof(fmt)-1,"%s [%s]" COLOR_END ": %s",  color, mesg_type, mesg_fmt);


	vfprintf(stream, fmt, ap);

}

static void write_mesg_label(FILE *stream, const char *color, const char *mesg_label, const char *mesg_fmt, va_list ap ) {
	
	static char fmt[4096] = {0};

	snprintf(fmt, sizeof(fmt)-1,"%s %s" COLOR_END "%s",  color, mesg_label, mesg_fmt);


	vfprintf(stream, fmt, ap);

}

void mesg_ok(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	write_mesg(stdout, COLOR_OK, "ok", fmt, va);
	va_end(va);
}

void mesg_ok_label(const char *label, const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	write_mesg_label(stdout, COLOR_OK, label, fmt, va);
	va_end(va);
}

void mesg_info(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	write_mesg(stdout, COLOR_INFO, "info", fmt, va);
	va_end(va);
}

void mesg_info_label(const char *label, const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	write_mesg_label(stdout, COLOR_INFO, label, fmt, va);
	va_end(va);
}


void mesg_warn(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	write_mesg(stderr, COLOR_WARN, "warn", fmt, va);
	va_end(va);
}

void mesg_warn_label(const char *label, const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	write_mesg_label(stdout, COLOR_WARN, label, fmt, va);
	va_end(va);
}



void mesg_error(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	write_mesg(stderr, COLOR_FAIL, "error", fmt, va);
	va_end(va);
}

void mesg_error_label(const char *label, const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	write_mesg_label(stderr, COLOR_FAIL, label, fmt, va);
	va_end(va);
}




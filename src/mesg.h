#ifndef __MESG_H__
#define __MESG_H__

void mesg_ok(const char *fmt, ...);

void mesg_ok_label(const char *label, const char *fmt, ...);

void mesg_info(const char *fmt, ...);

void mesg_info_label(const char *label, const char *fmt, ...);

void mesg_warn(const char *fmt, ...);

void mesg_warn_label(const char *label, const char *fmt, ...);

void mesg_error(const char *fmt, ...);

void mesg_error_label(const char *label, const char *fmt, ...);

#endif

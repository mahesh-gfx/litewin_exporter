#ifndef LITEWIN_LOG_H
#define LITEWIN_LOG_H

void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif

#ifndef _LOG_H
#define _LOG_H

enum log_level { level_error, level_info, level_debug, level_trace };

enum log_colour { colour_white, colour_yellow, colour_green, colour_red };

void log_msg(enum log_level lvl, const char *fmt, ...);

void log_highlight(int fd, enum log_colour, const char *fmt, ...);

void log_set_level(enum log_level);

#define logt(f, ...) log_msg(level_trace, f, ##__VA_ARGS__)
#define logd(f, ...) log_msg(level_debug, f, ##__VA_ARGS__)
#define logi(f, ...) log_msg(level_info, f, ##__VA_ARGS__)
#define loge(f, ...) log_msg(level_error, f, ##__VA_ARGS__)

#endif

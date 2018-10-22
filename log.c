#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

const char *log_colour_codes[] = {
    [colour_white]  = "\e[97m",
    [colour_yellow] = "\e[93m",
    [colour_green]  = "\e[92m",
    [colour_red]    = "\e[91m",
};

const int log_colour_lookup[] = {
    [level_trace] = colour_white,
    [level_debug] = colour_yellow,
    [level_info]  = colour_green,
    [level_error] = colour_red,
};

static void log_write_all(int fd, const char *buf, size_t len)
{
    ssize_t egress;

    while (len) {
        egress = write(fd, buf, len);
        if (egress < 0) {
            perror("write");
            return;
        }
        len -= egress;
        buf += egress;
    }
}

static void log_set_colour(int fd, enum log_colour colour)
{
    const char *code = log_colour_codes[colour];

    log_write_all(fd, code, strlen(code));
}

static void log_reset_colour(int fd)
{
    const char *reset_code = "\e[0m";

    log_write_all(fd, reset_code, 4);
}

void log_msg(enum log_level lvl, const char *fmt, ...)
{
    const int fd = fileno(stderr);
    va_list args;
    int rc;

    if (isatty(fd))
        log_set_colour(fd, log_colour_lookup[lvl]);

    log_write_all(fd, "[*] ", 4);

    if (isatty(fd))
        log_reset_colour(fd);

    va_start(args, fmt);
    rc = vdprintf(fd, fmt, args);
    va_end(args);
    if (rc < 0) { perror("vdprintf"); }
}

void log_highlight(int fd, enum log_colour colour, const char *fmt, ...)
{
    va_list args;
    int rc;

    if (isatty(fd))
        log_set_colour(fd, colour);

    va_start(args, fmt);
    rc = vdprintf(fd, fmt, args);
    va_end(args);
    if (rc < 0) { perror("vdprintf"); }

    if (isatty(fd))
        log_reset_colour(fd);
}

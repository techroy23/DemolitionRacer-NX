#ifndef SWITCH_DIAGNOSTICS_H
#define SWITCH_DIAGNOSTICS_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

typedef enum {
    DIAGNOSTICS_INVALID = -1,
    DIAGNOSTICS_ERROR,
    DIAGNOSTICS_INFO,
    DIAGNOSTICS_DEBUG
} DiagnosticLevel;

typedef struct {
    DiagnosticLevel level;
    bool fps_overlay;
    unsigned upscale;
} DiagnosticSettings;

bool diagnostics_parse_settings(FILE *settings, DiagnosticSettings *configuration);

#if defined(__GNUC__)
#define DIAGNOSTICS_FORMAT(format_index, first_argument_index) \
    __attribute__((__format__(__printf__, format_index, first_argument_index)))
#else
#define DIAGNOSTICS_FORMAT(format_index, first_argument_index)
#endif

DiagnosticLevel diagnostics_read_settings(FILE *settings);
const char *diagnostics_level_name(DiagnosticLevel level);
bool diagnostics_vwrite(FILE *log, DiagnosticLevel threshold, DiagnosticLevel level,
                        const char *format, va_list arguments) DIAGNOSTICS_FORMAT(4, 0);
bool diagnostics_write(FILE *log, DiagnosticLevel threshold, DiagnosticLevel level,
                       const char *format, ...) DIAGNOSTICS_FORMAT(4, 5);

#endif

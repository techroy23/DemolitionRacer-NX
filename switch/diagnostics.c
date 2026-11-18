#include "diagnostics.h"
#include <ctype.h>
#include <string.h>

static char *skip_spaces(char *cursor)
{
    while (*cursor && isspace((unsigned char)*cursor))
        ++cursor;
    return cursor;
}

const char *diagnostics_level_name(DiagnosticLevel level)
{
    switch (level)
    {
        case DIAGNOSTICS_ERROR: return "ERROR";
        case DIAGNOSTICS_INFO: return "INFO";
        case DIAGNOSTICS_DEBUG: return "DEBUG";
        default: return "INVALID";
    }
}

DiagnosticLevel diagnostics_read_settings(FILE *settings)
{
    // Only the top-level LOG scalar is supported. The game's original options
    // and memory card settings remain under the in-game UI's control.
    DiagnosticLevel level = DIAGNOSTICS_INFO;
    bool first_line = true, defined = false;
    char line[256];
    while (fgets(line, sizeof(line), settings))
    {
        if (!strchr(line, '\n') && !feof(settings))
            return DIAGNOSTICS_INVALID;
        char *key = line;
        if (first_line && strncmp(key, "\xef\xbb\xbf", 3) == 0)
            key += 3;
        first_line = false;
        if (strncmp(key, "LOG", 3) != 0 || (key[3] != ':' && !isspace((unsigned char)key[3])))
            continue;
        char *separator = skip_spaces(key + 3);
        if (*separator != ':' || !isspace((unsigned char)separator[1]) || defined)
            return DIAGNOSTICS_INVALID;
        char *value = skip_spaces(separator + 1);
        char quote = (*value == '\'' || *value == '"') ? *value++ : '\0';
        char *end = value;
        while (*end && (quote ? *end != quote : !isspace((unsigned char)*end)))
            ++end;
        char *tail = end;
        if (quote)
        {
            if (*tail != quote)
                return DIAGNOSTICS_INVALID;
            ++tail;
        }
        if (*tail && !isspace((unsigned char)*tail))
            return DIAGNOSTICS_INVALID;
        tail = skip_spaces(tail);
        if (*tail && *tail != '#')
            return DIAGNOSTICS_INVALID;
        *end = '\0';
        if (strcmp(value, "DEBUG") == 0)
            level = DIAGNOSTICS_DEBUG;
        else if (strcmp(value, "INFO") == 0)
            level = DIAGNOSTICS_INFO;
        else if (strcmp(value, "ERROR") == 0)
            level = DIAGNOSTICS_ERROR;
        else
            return DIAGNOSTICS_INVALID;
        defined = true;
    }
    return ferror(settings) ? DIAGNOSTICS_INVALID : level;
}

bool diagnostics_vwrite(FILE *log, DiagnosticLevel threshold, DiagnosticLevel level,
                        const char *format, va_list arguments)
{
    if (level > threshold)
        return true;
    return log && fprintf(log, "[%s] ", diagnostics_level_name(level)) >= 0 &&
           vfprintf(log, format, arguments) >= 0 && fputc('\n', log) != EOF && fflush(log) == 0;
}

bool diagnostics_write(FILE *log, DiagnosticLevel threshold, DiagnosticLevel level,
                       const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    bool written = diagnostics_vwrite(log, threshold, level, format, arguments);
    va_end(arguments);
    return written;
}

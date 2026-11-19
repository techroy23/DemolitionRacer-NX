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

bool diagnostics_parse_settings(FILE *settings, DiagnosticSettings *configuration)
{
    *configuration = (DiagnosticSettings){.level = DIAGNOSTICS_INFO, .display_filter = FRAMEBUFFER_FILTER_NEAREST};
    DiagnosticSettings parsed = *configuration;
    if (!settings)
        return true;
    const char *keys[] = {"LOG", "FPS_OVERLAY", "UPSCALE", "DISPLAY_FILTER", "DISPLAY_WORKER", "CAR_LOD", "TRACK_DISTANCE"};
    unsigned defined = 0;
    bool first_line = true;
    char line[256];
    while (fgets(line, sizeof(line), settings))
    {
        if (!strchr(line, '\n') && !feof(settings))
            return false;
        char *key = line;
        if (first_line && strncmp(key, "\xef\xbb\xbf", 3) == 0)
            key += 3;
        first_line = false;
        unsigned field;
        for (field = 0; field < sizeof(keys) / sizeof(*keys); ++field)
        {
            size_t length = strlen(keys[field]);
            if (strncmp(key, keys[field], length) == 0 &&
                (key[length] == ':' || !key[length] || isspace((unsigned char)key[length])))
                break;
        }
        if (field == sizeof(keys) / sizeof(*keys))
            continue;
        char *separator = skip_spaces(key + strlen(keys[field]));
        if (*separator != ':' || !isspace((unsigned char)separator[1]) || (defined & (1u << field)))
            return false;
        char *value = skip_spaces(separator + 1);
        char quote = (*value == '\'' || *value == '"') ? *value++ : '\0';
        char *end = value;
        while (*end && (quote ? *end != quote : !isspace((unsigned char)*end)))
            ++end;
        if (quote && *end != quote)
            return false;
        char *tail = end + (quote != '\0');
        if (*tail && !isspace((unsigned char)*tail))
            return false;
        tail = skip_spaces(tail);
        if (*tail && *tail != '#')
            return false;
        *end = '\0';
        switch (field)
        {
            case 0:
                if (!strcmp(value, "DEBUG")) parsed.level = DIAGNOSTICS_DEBUG;
                else if (!strcmp(value, "INFO")) parsed.level = DIAGNOSTICS_INFO;
                else if (!strcmp(value, "ERROR")) parsed.level = DIAGNOSTICS_ERROR;
                else return false;
                break;
            case 1:
                if (!strcmp(value, "true")) parsed.fps_overlay = true;
                else if (strcmp(value, "false")) return false;
                break;
            case 2:
                if (!strcmp(value, "FIT")) break;
                if (strlen(value) != 2 || value[0] != 'x' || value[1] < '2' || value[1] > '4') return false;
                parsed.upscale = (unsigned)(value[1] - '0');
                break;
            case 3:
                if (!strcmp(value, "BILINEAR")) parsed.display_filter = FRAMEBUFFER_FILTER_BILINEAR;
                else if (strcmp(value, "NEAREST")) return false;
                break;
            case 4:
                if (!strcmp(value, "true")) parsed.display_worker = true;
                else if (strcmp(value, "false")) return false;
                break;
            case 5:
            case 6:
                if (!strcmp(value, "EXTENDED"))
                {
                    if (field == 5) parsed.car_lod_extended = true;
                    else parsed.track_distance_extended = true;
                }
                else if (strcmp(value, "STOCK")) return false;
                break;
        }
        defined |= 1u << field;
    }
    if (ferror(settings))
        return false;
    *configuration = parsed;
    return true;
}

DiagnosticLevel diagnostics_read_settings(FILE *settings)
{
    DiagnosticSettings configuration;
    return diagnostics_parse_settings(settings, &configuration) ? configuration.level : DIAGNOSTICS_INVALID;
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

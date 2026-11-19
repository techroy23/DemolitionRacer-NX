#include "diagnostics.h"
#include <assert.h>
#include <string.h>

static FILE *stream_with(const char *text)
{
    FILE *stream = tmpfile();
    assert(stream && fputs(text, stream) >= 0);
    rewind(stream);
    return stream;
}

int main(void)
{
    FILE *options = stream_with("LOG: DEBUG\nFPS_OVERLAY: true\nUPSCALE: x4\nDISPLAY_FILTER: BILINEAR\n");
    DiagnosticSettings config;
    assert(diagnostics_parse_settings(options, &config));
    assert(config.level == DIAGNOSTICS_DEBUG && config.fps_overlay && config.upscale == 4);
    assert(config.display_filter == FRAMEBUFFER_FILTER_BILINEAR);
    fclose(options);
    options = stream_with("DISPLAY_FILTER: NEAREST\n");
    assert(diagnostics_parse_settings(options, &config));
    assert(config.display_filter == FRAMEBUFFER_FILTER_NEAREST);
    fclose(options);
    options = stream_with("DISPLAY_FILTER: MSAA\n");
    assert(!diagnostics_parse_settings(options, &config));
    assert(config.display_filter == FRAMEBUFFER_FILTER_NEAREST);
    fclose(options);
    options = stream_with("DISPLAY_FILTER: NEAREST\nDISPLAY_FILTER: BILINEAR\n");
    assert(!diagnostics_parse_settings(options, &config));
    fclose(options);
    assert(diagnostics_parse_settings(NULL, &config) && config.display_filter == FRAMEBUFFER_FILTER_NEAREST);
    assert(!config.display_worker && !config.car_lod_extended && !config.track_distance_extended);
    options = stream_with("DISPLAY_WORKER: true\nCAR_LOD: EXTENDED\nTRACK_DISTANCE: EXTENDED\n");
    assert(diagnostics_parse_settings(options, &config));
    assert(config.display_worker && config.car_lod_extended && config.track_distance_extended);
    fclose(options);
    options = stream_with("DISPLAY_WORKER: false\nCAR_LOD: STOCK\nTRACK_DISTANCE: STOCK\n");
    assert(diagnostics_parse_settings(options, &config));
    assert(!config.display_worker && !config.car_lod_extended && !config.track_distance_extended);
    fclose(options);
    options = stream_with("TRACK_DISTANCE: UNLIMITED\n");
    assert(!diagnostics_parse_settings(options, &config));
    fclose(options);
    options = stream_with("FPS_OVERLAY: false\nUPSCALE: x7\n");
    assert(!diagnostics_parse_settings(options, &config));
    assert(!config.fps_overlay && config.upscale == 0);
    fclose(options);
    FILE *settings = stream_with("# Local settings\r\nLOG: DEBUG # verbose\r\n");
    assert(diagnostics_read_settings(settings) == DIAGNOSTICS_DEBUG);
    fclose(settings);
    settings = stream_with("LOG: INFO\n");
    assert(diagnostics_read_settings(settings) == DIAGNOSTICS_INFO);
    fclose(settings);
    settings = stream_with("LOG: UNKNOWN\n");
    assert(diagnostics_read_settings(settings) == DIAGNOSTICS_INVALID);
    fclose(settings);
    settings = stream_with("LOG: DEBUG\nLOG: INFO\n");
    assert(diagnostics_read_settings(settings) == DIAGNOSTICS_INVALID);
    fclose(settings);
    settings = stream_with("\xef\xbb\xbf" "LOG: \"DEBUG\"\n");
    assert(diagnostics_read_settings(settings) == DIAGNOSTICS_DEBUG);
    fclose(settings);
    settings = stream_with("LOG: DEBUG#not-a-comment\n");
    assert(diagnostics_read_settings(settings) == DIAGNOSTICS_INVALID);
    fclose(settings);
    FILE *log = tmpfile();
    assert(log);
    assert(diagnostics_write(log, DIAGNOSTICS_INFO, DIAGNOSTICS_DEBUG, "hidden"));
    assert(diagnostics_write(log, DIAGNOSTICS_DEBUG, DIAGNOSTICS_DEBUG, "opened %s", "DATA/INTRO.STR"));
    assert(diagnostics_write(log, DIAGNOSTICS_DEBUG, DIAGNOSTICS_ERROR, "failure"));
    rewind(log);
    char lines[128];
    assert(fgets(lines, sizeof(lines), log) && strcmp(lines, "[DEBUG] opened DATA/INTRO.STR\n") == 0);
    assert(fgets(lines, sizeof(lines), log) && strcmp(lines, "[ERROR] failure\n") == 0);
    assert(fgetc(log) == EOF);
    fclose(log);
    return 0;
}

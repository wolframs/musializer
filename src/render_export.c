#include "render_export.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool enum_in_range(int value, int count)
{
    return value >= 0 && value < count;
}

void render_export_config_init(Render_Export_Config *config)
{
    if (config == NULL) return;
    *config = (Render_Export_Config) {
        .width = 1920,
        .height = 1080,
        .fps = 30,
        .quality = RENDER_QUALITY_HIGH,
        .supersample_factor = 2,
    };
}

Render_Export_Result render_export_config_set_resolution(
    Render_Export_Config *config, Render_Resolution resolution)
{
    static const uint32_t widths[] = {1280, 1920, 2560, 3840};
    static const uint32_t heights[] = {720, 1080, 1440, 2160};
    if (config == NULL) return RENDER_EXPORT_ERROR_NULL;
    if (!enum_in_range((int)resolution, RENDER_RESOLUTION_COUNT)) {
        return RENDER_EXPORT_ERROR_RESOLUTION;
    }
    config->width = widths[resolution];
    config->height = heights[resolution];
    return RENDER_EXPORT_OK;
}

Render_Export_Result render_export_config_set_frame_rate(
    Render_Export_Config *config, Render_Frame_Rate frame_rate)
{
    static const uint32_t rates[] = {24, 30, 60};
    if (config == NULL) return RENDER_EXPORT_ERROR_NULL;
    if (!enum_in_range((int)frame_rate, RENDER_FRAME_RATE_COUNT)) {
        return RENDER_EXPORT_ERROR_FRAME_RATE;
    }
    config->fps = rates[frame_rate];
    return RENDER_EXPORT_OK;
}

Render_Export_Result render_export_config_set_quality(
    Render_Export_Config *config, Render_Quality quality)
{
    static const uint32_t supersample[] = {1, 2, 2};
    if (config == NULL) return RENDER_EXPORT_ERROR_NULL;
    if (!enum_in_range((int)quality, RENDER_QUALITY_COUNT)) {
        return RENDER_EXPORT_ERROR_QUALITY;
    }
    config->quality = quality;
    config->supersample_factor = supersample[quality];
    return RENDER_EXPORT_OK;
}

Render_Export_Result render_export_config_validate(const Render_Export_Config *config)
{
    if (config == NULL) return RENDER_EXPORT_ERROR_NULL;
    if (config->width < 16 || config->height < 16 ||
        config->width > 7680 || config->height > 4320 ||
        (config->width & 1u) != 0 || (config->height & 1u) != 0) {
        return RENDER_EXPORT_ERROR_RESOLUTION;
    }
    if (config->fps < 1 || config->fps > 240) return RENDER_EXPORT_ERROR_FRAME_RATE;
    if (!enum_in_range((int)config->quality, RENDER_QUALITY_COUNT)) {
        return RENDER_EXPORT_ERROR_QUALITY;
    }
    if (config->supersample_factor < 1 || config->supersample_factor > 2 ||
        config->width > UINT32_MAX/config->supersample_factor ||
        config->height > UINT32_MAX/config->supersample_factor) {
        return RENDER_EXPORT_ERROR_SUPERSAMPLE;
    }
    return RENDER_EXPORT_OK;
}

const char *render_export_resolution_name(Render_Resolution resolution)
{
    static const char *const names[] = {"720p", "1080p", "1440p", "2160p"};
    return enum_in_range((int)resolution, RENDER_RESOLUTION_COUNT) ? names[resolution] : NULL;
}

const char *render_export_frame_rate_name(Render_Frame_Rate frame_rate)
{
    static const char *const names[] = {"24 fps", "30 fps", "60 fps"};
    return enum_in_range((int)frame_rate, RENDER_FRAME_RATE_COUNT) ? names[frame_rate] : NULL;
}

const char *render_export_quality_name(Render_Quality quality)
{
    static const char *const names[] = {"Balanced", "High", "Master"};
    return enum_in_range((int)quality, RENDER_QUALITY_COUNT) ? names[quality] : NULL;
}

const char *render_export_result_string(Render_Export_Result result)
{
    static const char *const names[] = {
        "ok", "null argument", "invalid output resolution", "invalid frame rate",
        "invalid quality", "invalid supersampling", "integer overflow",
        "invalid output path", "output buffer is too small",
        "render window is outside the track or not a positive finite range",
    };
    return enum_in_range((int)result, (int)(sizeof(names)/sizeof(names[0]))) ?
           names[result] : "unknown render export error";
}

Render_Export_Result render_export_total_frames(uint64_t frame_count,
                                                uint32_t sample_rate,
                                                uint32_t fps,
                                                uint64_t *total_frames)
{
    if (total_frames == NULL) return RENDER_EXPORT_ERROR_NULL;
    if (frame_count == 0 || sample_rate == 0) return RENDER_EXPORT_ERROR_FRAME_RATE;
    if (fps == 0 || fps > 240) return RENDER_EXPORT_ERROR_FRAME_RATE;
    if (frame_count > UINT64_MAX/fps) return RENDER_EXPORT_ERROR_OVERFLOW;
    uint64_t numerator = frame_count*fps;
    uint64_t result = numerator/sample_rate;
    if (numerator%sample_rate != 0) ++result;
    if (result == 0) result = 1;
    *total_frames = result;
    return RENDER_EXPORT_OK;
}

Render_Export_Result render_export_sample_cursor(uint64_t frame_index,
                                                 uint32_t sample_rate,
                                                 uint32_t fps,
                                                 uint64_t frame_count,
                                                 uint64_t *sample_cursor)
{
    if (sample_cursor == NULL) return RENDER_EXPORT_ERROR_NULL;
    if (sample_rate == 0 || fps == 0 || fps > 240) return RENDER_EXPORT_ERROR_FRAME_RATE;
    if (frame_index > UINT64_MAX/sample_rate) return RENDER_EXPORT_ERROR_OVERFLOW;
    uint64_t result = frame_index*sample_rate/fps;
    if (result > frame_count) result = frame_count;
    *sample_cursor = result;
    return RENDER_EXPORT_OK;
}

Render_Export_Result render_export_window_frames(uint64_t total_frames,
                                                 uint32_t fps,
                                                 double start_seconds,
                                                 double duration_seconds,
                                                 uint64_t *start_frame,
                                                 uint64_t *end_frame)
{
    if (start_frame == NULL || end_frame == NULL) return RENDER_EXPORT_ERROR_NULL;
    if (fps == 0 || fps > 240) return RENDER_EXPORT_ERROR_FRAME_RATE;
    if (total_frames == 0 ||
        !isfinite(start_seconds) || !isfinite(duration_seconds) ||
        start_seconds < 0.0 || duration_seconds <= 0.0) {
        return RENDER_EXPORT_ERROR_WINDOW;
    }
    // The start position stays comfortably inside double's exact-integer
    // range because it is bounded by total_frames, itself derived from a
    // decodable audio length.
    double start_position = start_seconds*(double)fps;
    if (!(start_position < (double)total_frames)) return RENDER_EXPORT_ERROR_WINDOW;
    uint64_t start = (uint64_t)start_position;
    double requested_end_seconds = start_seconds + duration_seconds;
    double requested_end_position = requested_end_seconds*(double)fps;
    uint64_t end = total_frames;
    if (isfinite(requested_end_seconds) &&
        isfinite(requested_end_position) &&
        requested_end_position < (double)total_frames) {
        end = (uint64_t)ceil(requested_end_position);
    }
    if (end <= start) end = start + 1;
    *start_frame = start;
    *end_frame = end;
    return RENDER_EXPORT_OK;
}

Render_Export_Result render_export_transport_duration_text(
    uint64_t total_frames, uint32_t fps, char *output, size_t capacity)
{
    if (output == NULL) return RENDER_EXPORT_ERROR_NULL;
    if (total_frames == 0 || fps == 0 || fps > 240) {
        return RENDER_EXPORT_ERROR_FRAME_RATE;
    }
    uint64_t whole_seconds = total_frames/fps;
    uint64_t remainder = total_frames%fps;
    uint64_t nanoseconds =
        (remainder*UINT64_C(1000000000) + fps/2u)/fps;
    if (nanoseconds == UINT64_C(1000000000)) {
        if (whole_seconds == UINT64_MAX) return RENDER_EXPORT_ERROR_OVERFLOW;
        ++whole_seconds;
        nanoseconds = 0;
    }
    int required = snprintf(NULL, 0, "%" PRIu64 ".%09" PRIu64,
                            whole_seconds, nanoseconds);
    if (required < 0 || (size_t)required >= capacity) {
        return RENDER_EXPORT_ERROR_BUFFER;
    }
    (void)snprintf(output, capacity, "%" PRIu64 ".%09" PRIu64,
                   whole_seconds, nanoseconds);
    return RENDER_EXPORT_OK;
}

Render_Export_Result render_export_target_scale(
    const Render_Export_Config *config, uint32_t target_width,
    uint32_t target_height, float *scale)
{
    if (config == NULL || scale == NULL) return RENDER_EXPORT_ERROR_NULL;
    Render_Export_Result valid = render_export_config_validate(config);
    if (valid != RENDER_EXPORT_OK) return valid;
    if (target_width == 0 || target_height == 0 ||
        target_width%config->width != 0 ||
        target_height%config->height != 0) {
        return RENDER_EXPORT_ERROR_RESOLUTION;
    }
    uint32_t width_scale = target_width/config->width;
    uint32_t height_scale = target_height/config->height;
    if (width_scale == 0 || width_scale != height_scale ||
        width_scale > config->supersample_factor) {
        return RENDER_EXPORT_ERROR_RESOLUTION;
    }
    *scale = (float)width_scale;
    return RENDER_EXPORT_OK;
}

static bool scene_name_is_safe(const char *name)
{
    if (name == NULL || name[0] == '\0') return false;
    for (size_t i = 0; name[i] != '\0'; ++i) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

Render_Export_Result render_export_suggest_path(const char *audio_path,
                                                const char *scene_name,
                                                const Render_Export_Config *config,
                                                char *output, size_t capacity)
{
    if (audio_path == NULL || scene_name == NULL || config == NULL || output == NULL) {
        return RENDER_EXPORT_ERROR_NULL;
    }
    if (audio_path[0] == '\0' || !scene_name_is_safe(scene_name)) {
        return RENDER_EXPORT_ERROR_PATH;
    }
    Render_Export_Result valid = render_export_config_validate(config);
    if (valid != RENDER_EXPORT_OK) return valid;

    const char *slash = strrchr(audio_path, '/');
    const char *backslash = strrchr(audio_path, '\\');
    const char *separator = slash;
    if (backslash != NULL && (separator == NULL || backslash > separator)) separator = backslash;
    const char *name = separator == NULL ? audio_path : separator + 1;
    const char *dot = strrchr(name, '.');
    size_t prefix_length = dot != NULL && dot != name ? (size_t)(dot - audio_path) : strlen(audio_path);
    if (prefix_length > INT_MAX) return RENDER_EXPORT_ERROR_PATH;
    int required = snprintf(NULL, 0, "%.*s-musializer-%s-%up%u.mp4",
                            (int)prefix_length, audio_path, scene_name,
                            config->height, config->fps);
    if (required < 0 || (size_t)required >= capacity) return RENDER_EXPORT_ERROR_BUFFER;
    (void)snprintf(output, capacity, "%.*s-musializer-%s-%up%u.mp4",
                   (int)prefix_length, audio_path, scene_name,
                   config->height, config->fps);
    return RENDER_EXPORT_OK;
}

bool render_export_temporary_path(const char *output_path,
                                  uint64_t process_id, uint64_t nonce,
                                  char **temporary_path)
{
    if (output_path == NULL || output_path[0] == '\0' || temporary_path == NULL ||
        process_id == 0 || nonce == 0) {
        return false;
    }

    static const char temporary_prefix[] = ".musializer-";
    static const char suffix_end[] = ".part.mp4";
    char identity[65];
    int identity_length = snprintf(identity, sizeof(identity),
                                   "%" PRIu64 "-%" PRIu64,
                                   process_id, nonce);
    if (identity_length < 0 || (size_t)identity_length >= sizeof(identity)) {
        return false;
    }

    const char *last_slash = strrchr(output_path, '/');
    const char *last_backslash = strrchr(output_path, '\\');
    const char *separator = last_slash;
    if (last_backslash != NULL && (separator == NULL || last_backslash > separator)) {
        separator = last_backslash;
    }
    size_t directory_length = separator == NULL ? 0 : (size_t)(separator - output_path) + 1;
    size_t name_length = sizeof(temporary_prefix) - 1 + (size_t)identity_length +
                         sizeof(suffix_end) - 1;
    if (directory_length > SIZE_MAX - name_length - 1) return false;

    char *path = malloc(directory_length + name_length + 1);
    if (path == NULL) return false;
    memcpy(path, output_path, directory_length);
    size_t offset = directory_length;
    memcpy(path + offset, temporary_prefix, sizeof(temporary_prefix) - 1);
    offset += sizeof(temporary_prefix) - 1;
    memcpy(path + offset, identity, (size_t)identity_length);
    offset += (size_t)identity_length;
    memcpy(path + offset, suffix_end, sizeof(suffix_end));

    *temporary_path = path;
    return true;
}

bool render_export_decoded_audio_path(const char *output_path,
                                      uint64_t process_id,
                                      uint64_t nonce,
                                      char *path, size_t capacity)
{
    if (output_path == NULL || output_path[0] == '\0' ||
        path == NULL || capacity == 0 || process_id == 0 || nonce == 0) {
        return false;
    }
    const char *slash = strrchr(output_path, '/');
    const char *backslash = strrchr(output_path, '\\');
    const char *separator = slash;
    if (backslash != NULL && (separator == NULL || backslash > separator)) {
        separator = backslash;
    }
    size_t directory_length = separator == NULL ? 0 :
                              (size_t)(separator - output_path) + 1;
    if (directory_length > INT_MAX) return false;
    int length = snprintf(NULL, 0,
                          "%.*s.musializer-audio-%" PRIu64 "-%" PRIu64 ".wav",
                          (int)directory_length, output_path, process_id, nonce);
    if (length <= 0 || (size_t)length >= capacity) return false;
    (void)snprintf(path, capacity,
                   "%.*s.musializer-audio-%" PRIu64 "-%" PRIu64 ".wav",
                   (int)directory_length, output_path, process_id, nonce);
    return true;
}

uint32_t render_export_finalize_grace_ms(bool cancellation)
{
    return cancellation ? UINT32_C(5000) : UINT32_C(300000);
}

Render_Export_Wait_Action render_export_wait_action(bool process_exited,
                                                    uint64_t elapsed_ms,
                                                    bool cancellation)
{
    if (process_exited) return RENDER_EXPORT_WAIT_COMPLETE;
    return elapsed_ms >= render_export_finalize_grace_ms(cancellation) ?
           RENDER_EXPORT_WAIT_TIMEOUT : RENDER_EXPORT_WAIT_CONTINUE;
}

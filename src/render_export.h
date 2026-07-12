#ifndef MUSIALIZER_RENDER_EXPORT_H_
#define MUSIALIZER_RENDER_EXPORT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum Render_Resolution {
    RENDER_RESOLUTION_720P,
    RENDER_RESOLUTION_1080P,
    RENDER_RESOLUTION_1440P,
    RENDER_RESOLUTION_2160P,
    RENDER_RESOLUTION_COUNT,
} Render_Resolution;

typedef enum Render_Frame_Rate {
    RENDER_FRAME_RATE_24,
    RENDER_FRAME_RATE_30,
    RENDER_FRAME_RATE_60,
    RENDER_FRAME_RATE_COUNT,
} Render_Frame_Rate;

typedef enum Render_Quality {
    RENDER_QUALITY_BALANCED,
    RENDER_QUALITY_HIGH,
    RENDER_QUALITY_MASTER,
    RENDER_QUALITY_COUNT,
} Render_Quality;

typedef struct Render_Export_Config {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    Render_Quality quality;
    uint32_t supersample_factor;
} Render_Export_Config;

typedef enum Render_Export_Result {
    RENDER_EXPORT_OK = 0,
    RENDER_EXPORT_ERROR_NULL,
    RENDER_EXPORT_ERROR_RESOLUTION,
    RENDER_EXPORT_ERROR_FRAME_RATE,
    RENDER_EXPORT_ERROR_QUALITY,
    RENDER_EXPORT_ERROR_SUPERSAMPLE,
    RENDER_EXPORT_ERROR_OVERFLOW,
    RENDER_EXPORT_ERROR_PATH,
    RENDER_EXPORT_ERROR_BUFFER,
} Render_Export_Result;

typedef enum Render_Export_Wait_Action {
    RENDER_EXPORT_WAIT_CONTINUE = 0,
    RENDER_EXPORT_WAIT_COMPLETE,
    RENDER_EXPORT_WAIT_TIMEOUT,
} Render_Export_Wait_Action;

void render_export_config_init(Render_Export_Config *config);
Render_Export_Result render_export_config_set_resolution(
    Render_Export_Config *config, Render_Resolution resolution);
Render_Export_Result render_export_config_set_frame_rate(
    Render_Export_Config *config, Render_Frame_Rate frame_rate);
Render_Export_Result render_export_config_set_quality(
    Render_Export_Config *config, Render_Quality quality);
Render_Export_Result render_export_config_validate(const Render_Export_Config *config);

const char *render_export_resolution_name(Render_Resolution resolution);
const char *render_export_frame_rate_name(Render_Frame_Rate frame_rate);
const char *render_export_quality_name(Render_Quality quality);
const char *render_export_result_string(Render_Export_Result result);

// Exact deterministic transport helpers. frame_count is decoded audio frames,
// not interleaved sample count. Results are untouched on failure.
Render_Export_Result render_export_total_frames(uint64_t frame_count,
                                                uint32_t sample_rate,
                                                uint32_t fps,
                                                uint64_t *total_frames);
Render_Export_Result render_export_sample_cursor(uint64_t frame_index,
                                                 uint32_t sample_rate,
                                                 uint32_t fps,
                                                 uint64_t frame_count,
                                                 uint64_t *sample_cursor);

// Formats the exact video transport duration for FFmpeg's output `-t` cap.
// Nine fractional digits are sufficient for every supported integer FPS.
Render_Export_Result render_export_transport_duration_text(
    uint64_t total_frames, uint32_t fps, char *output, size_t capacity);

// Reports the uniform target/output scale used for offline supersampling.
// Output remains untouched when the target is not an exact supported multiple.
Render_Export_Result render_export_target_scale(
    const Render_Export_Config *config, uint32_t target_width,
    uint32_t target_height, float *scale);

// Suggests a sibling path such as track-musializer-orbital-1080p30.mp4.
Render_Export_Result render_export_suggest_path(const char *audio_path,
                                                const char *scene_name,
                                                const Render_Export_Config *config,
                                                char *output, size_t capacity);

// Produces a sibling MP4 path that FFmpeg can infer without an explicit muxer.
// The caller owns *temporary_path and must free it. Output is untouched on
// failure. A per-process nonce keeps concurrent exports independent.
bool render_export_temporary_path(const char *output_path,
                                  uint64_t process_id, uint64_t nonce,
                                  char **temporary_path);

// Produces an output-directory sibling for a decoded PCM WAV used by both the
// analyzer and FFmpeg. process_id + nonce keep concurrent jobs independent.
bool render_export_decoded_audio_path(const char *output_path,
                                      uint64_t process_id,
                                      uint64_t nonce,
                                      char *path, size_t capacity);

// Encoder finalization may relocate a large 4K MP4 for faststart, so normal
// completion gets a generous grace period. Cancellation/forced termination is
// deliberately much shorter. The state helper makes boundary behavior shared
// and directly testable across POSIX and Windows backends.
uint32_t render_export_finalize_grace_ms(bool cancellation);
Render_Export_Wait_Action render_export_wait_action(bool process_exited,
                                                    uint64_t elapsed_ms,
                                                    bool cancellation);

#endif // MUSIALIZER_RENDER_EXPORT_H_

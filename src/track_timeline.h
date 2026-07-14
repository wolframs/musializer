#ifndef MUSIALIZER_TRACK_TIMELINE_H_
#define MUSIALIZER_TRACK_TIMELINE_H_

#include <stdbool.h>
#include <stddef.h>

#define TRACK_TIMELINE_MAX_BINS 2048

typedef struct {
    float minimum;
    float maximum;
} Track_Timeline_Bin;

typedef struct {
    Track_Timeline_Bin bins[TRACK_TIMELINE_MAX_BINS];
    size_t count;
} Track_Timeline_Waveform;

// Builds a peak-normalized display envelope from interleaved floating-point
// PCM. Malformed samples are ignored and silence remains a zero-height line.
size_t track_timeline_build_waveform(const float *samples,
                                     size_t frame_count,
                                     size_t channel_count,
                                     Track_Timeline_Bin *bins,
                                     size_t bin_capacity);

// Exact, finite transport helpers shared by buttons, keyboard navigation, and
// pointer seeking. Invalid geometry leaves the current position unchanged.
double track_timeline_seek_relative(double current_seconds,
                                    double delta_seconds,
                                    double duration_seconds);
double track_timeline_seek_from_x(double current_seconds,
                                  double pointer_x,
                                  double left,
                                  double width,
                                  double duration_seconds);

// Raylib cannot seek tracker-module streams. Keep transport affordances
// truthful for those extensions while preserving all decoded audio formats.
bool track_timeline_path_is_seekable(const char *path);

#endif // MUSIALIZER_TRACK_TIMELINE_H_

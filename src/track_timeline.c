#include "track_timeline.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static bool timeline_suffix_equal(const char *value, const char *suffix)
{
    size_t value_length = strlen(value);
    size_t suffix_length = strlen(suffix);
    if (value_length < suffix_length) return false;
    const char *at = value + value_length - suffix_length;
    for (size_t index = 0; index < suffix_length; ++index) {
        char left = at[index];
        char right = suffix[index];
        if (left >= 'A' && left <= 'Z') left = (char)(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z') right = (char)(right - 'A' + 'a');
        if (left != right) return false;
    }
    return true;
}

bool track_timeline_path_is_seekable(const char *path)
{
    if (path == NULL || path[0] == '\0') return false;
    return !timeline_suffix_equal(path, ".xm") &&
           !timeline_suffix_equal(path, ".mod");
}

static double clamp_position(double seconds, double duration_seconds)
{
    if (!isfinite(duration_seconds) || duration_seconds <= 0.0) return 0.0;
    if (!isfinite(seconds)) return 0.0;
    if (seconds <= 0.0) return 0.0;
    if (seconds >= duration_seconds) return duration_seconds;
    return seconds;
}

size_t track_timeline_build_waveform(const float *samples,
                                     size_t frame_count,
                                     size_t channel_count,
                                     Track_Timeline_Bin *bins,
                                     size_t bin_capacity)
{
    if (samples == NULL || bins == NULL || frame_count == 0 ||
        channel_count == 0 || bin_capacity == 0 ||
        frame_count > SIZE_MAX/channel_count) return 0;

    size_t count = frame_count < bin_capacity ? frame_count : bin_capacity;
    size_t frame_step = frame_count/count;
    size_t frame_remainder = frame_count%count;
    float global_peak = 0.0f;
    for (size_t bin_index = 0; bin_index < count; ++bin_index) {
        size_t first_frame = bin_index*frame_step +
            (bin_index*frame_remainder)/count;
        size_t next_bin = bin_index + 1u;
        size_t end_frame = next_bin*frame_step +
            (next_bin*frame_remainder)/count;
        if (end_frame <= first_frame) end_frame = first_frame + 1u;

        float minimum = 0.0f;
        float maximum = 0.0f;
        for (size_t frame = first_frame; frame < end_frame; ++frame) {
            size_t sample_index = frame*channel_count;
            for (size_t channel = 0; channel < channel_count; ++channel) {
                float value = samples[sample_index + channel];
                if (!isfinite(value)) continue;
                if (value < -1.0f) value = -1.0f;
                if (value > 1.0f) value = 1.0f;
                if (value < minimum) minimum = value;
                if (value > maximum) maximum = value;
            }
        }
        bins[bin_index] = (Track_Timeline_Bin) {
            .minimum = minimum,
            .maximum = maximum,
        };
        float magnitude = fmaxf(fabsf(minimum), fabsf(maximum));
        if (magnitude > global_peak) global_peak = magnitude;
    }

    if (global_peak > 0.000001f) {
        float scale = 1.0f/global_peak;
        for (size_t i = 0; i < count; ++i) {
            bins[i].minimum *= scale;
            bins[i].maximum *= scale;
        }
    }
    return count;
}

double track_timeline_seek_relative(double current_seconds,
                                    double delta_seconds,
                                    double duration_seconds)
{
    double current = clamp_position(current_seconds, duration_seconds);
    if (!isfinite(current_seconds) || !isfinite(delta_seconds) ||
        !isfinite(duration_seconds) ||
        duration_seconds <= 0.0) return current;
    if (delta_seconds >= duration_seconds - current) return duration_seconds;
    if (delta_seconds <= -current) return 0.0;
    return current + delta_seconds;
}

double track_timeline_seek_from_x(double current_seconds,
                                  double pointer_x,
                                  double left,
                                  double width,
                                  double duration_seconds)
{
    if (!isfinite(pointer_x) || !isfinite(left) || !isfinite(width) ||
        width <= 0.0 || !isfinite(duration_seconds) || duration_seconds <= 0.0) {
        return clamp_position(current_seconds, duration_seconds);
    }
    double fraction = (pointer_x - left)/width;
    return clamp_position(fraction*duration_seconds, duration_seconds);
}

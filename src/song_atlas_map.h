#ifndef MUSIALIZER_SONG_ATLAS_MAP_H_
#define MUSIALIZER_SONG_ATLAS_MAP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SONG_ATLAS_BAND_COUNT 28
#define SONG_ATLAS_BASE_SLICES 192
#define SONG_ATLAS_MAX_DETAIL 3
#define SONG_ATLAS_MAX_SLICES (SONG_ATLAS_BASE_SLICES*SONG_ATLAS_MAX_DETAIL)

typedef struct {
    float bands[SONG_ATLAS_BAND_COUNT];
    float rms;
    float flux;
    bool onset;
} Song_Atlas_Slice;

typedef struct {
    Song_Atlas_Slice slices[SONG_ATLAS_MAX_SLICES];
    size_t count;
    double duration_seconds;
} Song_Atlas_Map;

// Builds a bounded whole-track spectral map from interleaved floating-point
// PCM. Time is sampled uniformly; frequency bands follow the preview
// analyzer's logarithmic partition. Invalid input clears the destination.
size_t song_atlas_map_build(const float *samples,
                            size_t frame_count,
                            size_t channel_count,
                            uint32_t sample_rate,
                            Song_Atlas_Map *map);

size_t song_atlas_map_slice_count(size_t frame_count, uint32_t sample_rate);
size_t song_atlas_map_render_sample_count(size_t available,
                                          size_t detail_level);
size_t song_atlas_map_render_sample_index(size_t first, size_t available,
                                          size_t sample_count, size_t sample);
float song_atlas_map_render_distance(float source_distance);

bool song_atlas_map_valid(const Song_Atlas_Map *map);

#endif // MUSIALIZER_SONG_ATLAS_MAP_H_

#include "song_atlas_map.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "audio_analyzer.h"

static float atlas_map_clamp01(float value)
{
    if (!isfinite(value)) return 0.0f;
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float atlas_map_resample_band(const AudioSpectrumView *spectrum,
                                     size_t destination)
{
    if (spectrum == NULL || spectrum->logarithmic == NULL ||
        spectrum->band_count == 0) return 0.0f;
    size_t begin = destination*spectrum->band_count/SONG_ATLAS_BAND_COUNT;
    size_t end = (destination + 1)*spectrum->band_count/SONG_ATLAS_BAND_COUNT;
    if (end <= begin) end = begin + 1;
    if (begin >= spectrum->band_count) begin = spectrum->band_count - 1;
    if (end > spectrum->band_count) end = spectrum->band_count;

    float peak = 0.0f;
    for (size_t i = begin; i < end; ++i) {
        float value = atlas_map_clamp01(spectrum->logarithmic[i]);
        if (value > peak) peak = value;
    }
    return peak;
}

static size_t atlas_map_slice_count(size_t frame_count, uint32_t sample_rate)
{
    double duration = (double)frame_count/(double)sample_rate;
    double desired = ceil(duration*2.0);
    if (desired < 24.0) desired = 24.0;
    if (desired > SONG_ATLAS_MAX_SLICES) desired = SONG_ATLAS_MAX_SLICES;
    return (size_t)desired;
}

static void atlas_map_smooth(Song_Atlas_Map *map)
{
    if (map == NULL || map->count < 2) return;
    float previous[SONG_ATLAS_BAND_COUNT] = {0};
    for (size_t band = 0; band < SONG_ATLAS_BAND_COUNT; ++band) {
        previous[band] = map->slices[0].bands[band];
    }
    for (size_t i = 1; i < map->count; ++i) {
        for (size_t band = 0; band < SONG_ATLAS_BAND_COUNT; ++band) {
            float current = map->slices[i].bands[band];
            float spatial = current*0.60f;
            float weight = 0.60f;
            if (band > 0) {
                spatial += map->slices[i].bands[band - 1]*0.20f;
                weight += 0.20f;
            }
            if (band + 1 < SONG_ATLAS_BAND_COUNT) {
                spatial += map->slices[i].bands[band + 1]*0.20f;
                weight += 0.20f;
            }
            spatial /= weight;
            float response = spatial > previous[band] ? 0.72f : 0.38f;
            float filtered = previous[band] + (spatial - previous[band])*response;
            previous[band] = filtered;
            map->slices[i].bands[band] = filtered;
        }
    }
}

static void atlas_map_finalize_dynamics(Song_Atlas_Map *map)
{
    float maximum_rms = 0.0f;
    float maximum_flux = 0.0f;
    for (size_t i = 0; i < map->count; ++i) {
        if (map->slices[i].rms > maximum_rms) maximum_rms = map->slices[i].rms;
    }
    for (size_t i = 0; i < map->count; ++i) {
        float normalized_rms = maximum_rms > 1.0e-8f ?
            sqrtf(map->slices[i].rms/maximum_rms) : 0.0f;
        map->slices[i].rms = atlas_map_clamp01(normalized_rms);
        float loudness = 0.18f + 0.82f*map->slices[i].rms;
        for (size_t band = 0; band < SONG_ATLAS_BAND_COUNT; ++band) {
            map->slices[i].bands[band] = atlas_map_clamp01(
                map->slices[i].bands[band]*loudness);
        }
        if (i == 0) continue;
        float flux = 0.0f;
        for (size_t band = 0; band < SONG_ATLAS_BAND_COUNT; ++band) {
            float rise = map->slices[i].bands[band] - map->slices[i - 1].bands[band];
            if (rise > 0.0f) flux += rise;
        }
        map->slices[i].flux = flux/(float)SONG_ATLAS_BAND_COUNT;
        if (map->slices[i].flux > maximum_flux) maximum_flux = map->slices[i].flux;
    }
    size_t last_onset = SIZE_MAX;
    for (size_t i = 0; i < map->count; ++i) {
        float normalized = maximum_flux > 1.0e-8f ?
            map->slices[i].flux/maximum_flux : 0.0f;
        map->slices[i].flux = atlas_map_clamp01(normalized);
        bool separated = last_onset == SIZE_MAX || i - last_onset >= 3;
        map->slices[i].onset = separated && normalized >= 0.62f &&
                               map->slices[i].rms >= 0.16f;
        if (map->slices[i].onset) last_onset = i;
    }
}

size_t song_atlas_map_build(const float *samples,
                            size_t frame_count,
                            size_t channel_count,
                            uint32_t sample_rate,
                            Song_Atlas_Map *map)
{
    if (map == NULL) return 0;
    memset(map, 0, sizeof(*map));
    if (samples == NULL || frame_count == 0 || channel_count == 0 ||
        channel_count > UINT32_MAX ||
        sample_rate == 0 || frame_count > SIZE_MAX/channel_count) return 0;

    AudioAnalyzer *analyzer = calloc(1, sizeof(*analyzer));
    if (analyzer == NULL) return 0;
    AudioAnalyzerConfig config = {
        .sample_rate = sample_rate,
        .channel_count = (uint32_t)channel_count,
        .channel_mode = AUDIO_ANALYZER_CHANNEL_MIX,
        .selected_channel = 0,
    };
    if (!audio_analyzer_init(analyzer, config)) {
        free(analyzer);
        return 0;
    }

    map->count = atlas_map_slice_count(frame_count, sample_rate);
    map->duration_seconds = (double)frame_count/(double)sample_rate;
    for (size_t i = 0; i < map->count; ++i) {
        double position = map->count > 1 ?
            (double)i*(double)(frame_count - 1)/(double)(map->count - 1) : 0.0;
        size_t center = (size_t)position;
        size_t half = AUDIO_ANALYZER_FFT_SIZE/2;
        size_t begin = center > half ? center - half : 0;
        size_t end = begin + AUDIO_ANALYZER_FFT_SIZE;
        if (end > frame_count) {
            end = frame_count;
            begin = end > AUDIO_ANALYZER_FFT_SIZE ? end - AUDIO_ANALYZER_FFT_SIZE : 0;
        }
        size_t count = end - begin;
        audio_analyzer_reset(analyzer);
        if (audio_analyzer_push_interleaved(
                analyzer, samples + begin*channel_count, count) != count ||
            !audio_analyzer_analyze(analyzer, 0.1f)) {
            memset(map, 0, sizeof(*map));
            free(analyzer);
            return 0;
        }
        AudioSpectrumView spectrum = audio_analyzer_spectrum(analyzer);
        Song_Atlas_Slice *slice = &map->slices[i];
        double square_sum = 0.0;
        for (size_t frame = begin; frame < end; ++frame) {
            double mono = 0.0;
            for (size_t channel = 0; channel < channel_count; ++channel) {
                float sample = samples[frame*channel_count + channel];
                if (isfinite(sample)) {
                    if (sample < -1.0f) sample = -1.0f;
                    if (sample > 1.0f) sample = 1.0f;
                    mono += sample;
                }
            }
            mono /= (double)channel_count;
            square_sum += mono*mono;
        }
        slice->rms = count > 0 ? (float)(square_sum/(double)count) : 0.0f;
        for (size_t band = 0; band < SONG_ATLAS_BAND_COUNT; ++band) {
            slice->bands[band] = atlas_map_resample_band(&spectrum, band);
        }
    }
    free(analyzer);
    atlas_map_smooth(map);
    atlas_map_finalize_dynamics(map);
    return map->count;
}

bool song_atlas_map_valid(const Song_Atlas_Map *map)
{
    if (map == NULL || map->count < 2 || map->count > SONG_ATLAS_MAX_SLICES ||
        !isfinite(map->duration_seconds) || map->duration_seconds <= 0.0) {
        return false;
    }
    for (size_t i = 0; i < map->count; ++i) {
        if (!isfinite(map->slices[i].rms) || map->slices[i].rms < 0.0f ||
            map->slices[i].rms > 1.0f || !isfinite(map->slices[i].flux) ||
            map->slices[i].flux < 0.0f || map->slices[i].flux > 1.0f) {
            return false;
        }
        for (size_t band = 0; band < SONG_ATLAS_BAND_COUNT; ++band) {
            float value = map->slices[i].bands[band];
            if (!isfinite(value) || value < 0.0f || value > 1.0f) return false;
        }
    }
    return true;
}

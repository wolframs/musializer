#ifndef MUSIALIZER_AUDIO_ANALYZER_H_
#define MUSIALIZER_AUDIO_ANALYZER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_ANALYZER_FFT_SIZE (1u << 13)
#define AUDIO_ANALYZER_MAX_BANDS 256u

typedef enum AudioAnalyzerChannelMode {
    AUDIO_ANALYZER_CHANNEL_MIX,
    AUDIO_ANALYZER_CHANNEL_SELECT,
} AudioAnalyzerChannelMode;

typedef struct AudioAnalyzerConfig {
    uint32_t sample_rate;
    uint32_t channel_count;
    AudioAnalyzerChannelMode channel_mode;
    uint32_t selected_channel;
} AudioAnalyzerConfig;

typedef struct AudioComplex {
    float real;
    float imaginary;
} AudioComplex;

/*
 * The analyzer owns all of its working memory. Keeping the representation
 * public lets callers embed it in hot-reloaded state without an allocator.
 * Treat fields below config as read-only outside audio_analyzer.c.
 */
typedef struct AudioAnalyzer {
    AudioAnalyzerConfig config;
    float input[AUDIO_ANALYZER_FFT_SIZE];
    float windowed[AUDIO_ANALYZER_FFT_SIZE];
    AudioComplex fft[AUDIO_ANALYZER_FFT_SIZE];
    float logarithmic[AUDIO_ANALYZER_MAX_BANDS];
    float smooth[AUDIO_ANALYZER_MAX_BANDS];
    float smear[AUDIO_ANALYZER_MAX_BANDS];
    uint16_t band_first_bin[AUDIO_ANALYZER_MAX_BANDS];
    uint16_t band_end_bin[AUDIO_ANALYZER_MAX_BANDS];
    size_t write_cursor;
    size_t sample_count;
    size_t band_count;
} AudioAnalyzer;

typedef struct AudioSpectrumView {
    const float *logarithmic;
    const float *smooth;
    const float *smear;
    const uint16_t *band_first_bin;
    const uint16_t *band_end_bin;
    size_t band_count;
    uint32_t sample_rate;
} AudioSpectrumView;

bool audio_analyzer_init(AudioAnalyzer *analyzer, AudioAnalyzerConfig config);
void audio_analyzer_reset(AudioAnalyzer *analyzer);

/* Pushes interleaved frames and returns the number consumed. */
size_t audio_analyzer_push_interleaved(AudioAnalyzer *analyzer,
                                       const float *samples,
                                       size_t frame_count);

/* Pushes already-mixed mono samples and returns the number consumed. */
size_t audio_analyzer_push_mono(AudioAnalyzer *analyzer,
                                const float *samples,
                                size_t sample_count);

/* dt_seconds is the deterministic scene/sample-clock delta for smoothing. */
bool audio_analyzer_analyze(AudioAnalyzer *analyzer, float dt_seconds);
AudioSpectrumView audio_analyzer_spectrum(const AudioAnalyzer *analyzer);
bool audio_analyzer_settled(const AudioAnalyzer *analyzer, float epsilon);

/* Bin boundaries are half-open; frequency values are in Hz. */
float audio_analyzer_bin_frequency(const AudioAnalyzer *analyzer, size_t bin);

#endif

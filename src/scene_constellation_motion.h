#ifndef MUSIALIZER_SCENE_CONSTELLATION_MOTION_H_
#define MUSIALIZER_SCENE_CONSTELLATION_MOTION_H_

#include <stdbool.h>

typedef struct Constellation_Motion_Input {
    double time_seconds;
    float delta_seconds;
    float rms;
    float spectral_flux;
    bool onset;
} Constellation_Motion_Input;

typedef struct Constellation_Motion {
    bool initialized;
    double last_time_seconds;
    float energy;
    float flux;
    float onset_pulse;
} Constellation_Motion;

void constellation_motion_init(Constellation_Motion *motion);
void constellation_motion_update(Constellation_Motion *motion,
                                 const Constellation_Motion_Input *input);

#endif // MUSIALIZER_SCENE_CONSTELLATION_MOTION_H_

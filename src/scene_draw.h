#ifndef MUSIALIZER_SCENE_DRAW_H_
#define MUSIALIZER_SCENE_DRAW_H_

#include <math.h>

#include <raylib.h>

// GL line primitives are implementation-defined and commonly rasterize as a
// single aliased pixel.  A small camera-space tube has stable world-space
// thickness, participates in depth testing, and is smoothed by both preview
// MSAA and the deterministic offline supersampling pass.
static inline void scene_draw_tube(Vector3 start, Vector3 end, float radius,
                                   int sides, Color color)
{
    float dx = end.x - start.x;
    float dy = end.y - start.y;
    float dz = end.z - start.z;
    if (!isfinite(radius) || radius <= 0.0f ||
        dx*dx + dy*dy + dz*dz <= 1.0e-10f) {
        return;
    }
    if (sides < 3) sides = 3;
    DrawCylinderEx(start, end, radius, radius, sides, color);
}

#endif // MUSIALIZER_SCENE_DRAW_H_

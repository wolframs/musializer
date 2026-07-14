#include "scene.h"

#include <stdlib.h>
#include <string.h>

extern const Scene_Descriptor scene_spectrum_descriptor;
extern const Scene_Descriptor scene_pulse_field_descriptor;
extern const Scene_Descriptor scene_orbital_lattice_descriptor;
extern const Scene_Descriptor scene_ascii_field_descriptor;
extern const Scene_Descriptor scene_song_atlas_descriptor;
extern const Scene_Descriptor scene_spectral_terrarium_descriptor;
extern const Scene_Descriptor scene_constellation_descriptor;
extern const Scene_Descriptor scene_cadence_descriptor;
extern const Scene_Descriptor scene_loom_descriptor;

static const Scene_Descriptor *const scene_registry[COUNT_SCENES] = {
    [SCENE_SPECTRUM] = &scene_spectrum_descriptor,
    [SCENE_PULSE_FIELD] = &scene_pulse_field_descriptor,
    [SCENE_ORBITAL_LATTICE] = &scene_orbital_lattice_descriptor,
    [SCENE_ASCII_FIELD] = &scene_ascii_field_descriptor,
    [SCENE_SONG_ATLAS] = &scene_song_atlas_descriptor,
    [SCENE_SPECTRAL_TERRARIUM] = &scene_spectral_terrarium_descriptor,
    [SCENE_CONSTELLATION] = &scene_constellation_descriptor,
    [SCENE_CADENCE] = &scene_cadence_descriptor,
    [SCENE_LOOM] = &scene_loom_descriptor,
};

static bool scene_id_valid(Scene_Id id)
{
    return 0 <= id && id < COUNT_SCENES && scene_registry[id] != NULL;
}

const Scene_Descriptor *scene_descriptor(Scene_Id id)
{
    if (!scene_id_valid(id)) return NULL;
    return scene_registry[id];
}

const char *scene_name(Scene_Id id)
{
    const Scene_Descriptor *descriptor = scene_descriptor(id);
    return descriptor ? descriptor->name : "Unknown";
}

static bool scene_allocate_state(Scene_Instance *scene, const Scene_Descriptor *descriptor)
{
    scene->state = NULL;
    scene->state_size = descriptor->state_size;
    scene->state_version = descriptor->state_version;

    if (descriptor->state_size > 0) {
        scene->state = calloc(1, descriptor->state_size);
        if (scene->state == NULL) return false;
    }

    if (descriptor->init) descriptor->init(scene->state, scene->seed);
    return true;
}

bool scene_instance_init(Scene_Instance *scene, Scene_Id id, uint64_t seed)
{
    if (scene == NULL) return false;
    memset(scene, 0, sizeof(*scene));

    const Scene_Descriptor *descriptor = scene_descriptor(id);
    if (descriptor == NULL) return false;

    scene->id = id;
    scene->seed = seed;
    return scene_allocate_state(scene, descriptor);
}

void scene_instance_unload(Scene_Instance *scene)
{
    if (scene == NULL) return;

    const Scene_Descriptor *descriptor = scene_descriptor(scene->id);
    if (descriptor && descriptor->unload && scene->state) {
        descriptor->unload(scene->state);
    }
    free(scene->state);
    memset(scene, 0, sizeof(*scene));
}

bool scene_instance_rebind(Scene_Instance *scene)
{
    if (scene == NULL) return false;

    const Scene_Descriptor *descriptor = scene_descriptor(scene->id);
    if (descriptor == NULL) return false;

    if (scene->state_version == descriptor->state_version &&
        scene->state_size == descriptor->state_size &&
        (scene->state != NULL || scene->state_size == 0)) {
        return true;
    }

    // The old descriptor may have disappeared during hot reload, so it is not
    // safe to call its unload callback here. Scene state must own only plain
    // memory or resources released by the plug's pre-reload hook.
    free(scene->state);
    scene->state = NULL;
    return scene_allocate_state(scene, descriptor);
}

bool scene_instance_select(Scene_Instance *scene, Scene_Id id, uint64_t seed)
{
    if (scene == NULL) return false;
    Scene_Instance replacement;
    if (!scene_instance_init(&replacement, id, seed)) return false;
    scene_instance_unload(scene);
    *scene = replacement;
    return true;
}

void scene_instance_update(Scene_Instance *scene, const Scene_Frame *frame)
{
    if (scene == NULL || frame == NULL) return;
    const Scene_Descriptor *descriptor = scene_descriptor(scene->id);
    if (descriptor && descriptor->update) descriptor->update(scene->state, frame);
}

void scene_instance_draw(const Scene_Instance *scene, const Scene_Frame *frame, const Scene_Renderer *renderer, Rectangle boundary)
{
    if (scene == NULL || frame == NULL || renderer == NULL) return;
    const Scene_Descriptor *descriptor = scene_descriptor(scene->id);
    if (descriptor && descriptor->draw) {
        descriptor->draw(scene->state, frame, renderer, boundary);
    }
}

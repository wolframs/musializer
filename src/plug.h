#ifndef PLUG_H_
#define PLUG_H_

#include <stdbool.h>

#define LIST_OF_PLUGS \
    PLUG(plug_init, void, void) \
    PLUG(plug_pre_reload, void*, void) \
    PLUG(plug_post_reload, void, void*) \
    PLUG(plug_load_resource, void*, const char*, size_t*) \
    PLUG(plug_free_resource, void, void*) \
    PLUG(plug_load_track, bool, const char*) \
    PLUG(plug_load_ascii_image, bool, const char*) \
    PLUG(plug_select_scene, bool, const char*) \
    PLUG(plug_start_render, bool, const char*) \
    PLUG(plug_render_active, bool, void) \
    PLUG(plug_render_failed, bool, void) \
    PLUG(plug_shutdown, void, void) \
    PLUG(plug_update, void, void)

#define PLUG(name, ret, ...) typedef ret (name##_t)(__VA_ARGS__);
LIST_OF_PLUGS
#undef PLUG

#endif // PLUG_H_

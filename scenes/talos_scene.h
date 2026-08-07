#pragma once

#include <gui/scene_manager.h>

// Generate the scene id enum
#define ADD_SCENE(prefix, name, id) TalosScene##id,
typedef enum {
#include "talos_scene_config.h"
    TalosSceneNum,
} TalosScene;
#undef ADD_SCENE

extern const SceneManagerHandlers talos_scene_handlers;

// Generate scene handler prototypes
#define ADD_SCENE(prefix, name, id)                                            \
    void prefix##_scene_##name##_on_enter(void* context);                      \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent e); \
    void prefix##_scene_##name##_on_exit(void* context);
#include "talos_scene_config.h"
#undef ADD_SCENE

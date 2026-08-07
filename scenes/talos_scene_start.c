#include "../talos_i.h"

typedef enum {
    StartIndexGrade,
    StartIndexKeyring,
    StartIndexSettings,
    StartIndexAbout,
} StartIndex;

static void talos_scene_start_submenu_cb(void* context, uint32_t index) {
    TalosApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void talos_scene_start_on_enter(void* context) {
    TalosApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "Talos");
    submenu_add_item(submenu, "Grade a Key", StartIndexGrade, talos_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "Keyring Log", StartIndexKeyring, talos_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "Settings", StartIndexSettings, talos_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "About", StartIndexAbout, talos_scene_start_submenu_cb, app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, TalosSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, TalosViewSubmenu);
}

bool talos_scene_start_on_event(void* context, SceneManagerEvent event) {
    TalosApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, TalosSceneStart, event.event);
        switch(event.event) {
        case StartIndexGrade:
            scene_manager_next_scene(app->scene_manager, TalosSceneScan);
            consumed = true;
            break;
        case StartIndexKeyring:
            scene_manager_next_scene(app->scene_manager, TalosSceneKeyring);
            consumed = true;
            break;
        case StartIndexSettings:
            scene_manager_next_scene(app->scene_manager, TalosSceneSettings);
            consumed = true;
            break;
        case StartIndexAbout:
            scene_manager_next_scene(app->scene_manager, TalosSceneAbout);
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

void talos_scene_start_on_exit(void* context) {
    TalosApp* app = context;
    submenu_reset(app->submenu);
}

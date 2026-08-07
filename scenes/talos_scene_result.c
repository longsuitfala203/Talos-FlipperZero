#include "../talos_i.h"

static void talos_scene_result_cb(void* context, ResultEvent event) {
    TalosApp* app = context;
    view_dispatcher_send_custom_event(
        app->view_dispatcher,
        (event == ResultEventReport) ? TalosCustomEventReport : TalosCustomEventRescan);
}

void talos_scene_result_on_enter(void* context) {
    TalosApp* app = context;

    result_view_set_result(app->result_view, &app->grade, &app->capture.reading);
    result_view_set_callback(app->result_view, talos_scene_result_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TalosViewResult);
}

bool talos_scene_result_on_event(void* context, SceneManagerEvent event) {
    TalosApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case TalosCustomEventReport:
            scene_manager_next_scene(app->scene_manager, TalosSceneReport);
            consumed = true;
            break;
        case TalosCustomEventRescan:
            /* Back to the (transient) scan scene, which re-arms the contact. */
            scene_manager_search_and_switch_to_previous_scene(app->scene_manager, TalosSceneScan);
            consumed = true;
            break;
        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        /* Skip the transient scan scene and land back on the main menu. */
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, TalosSceneStart);
        consumed = true;
    }
    return consumed;
}

void talos_scene_result_on_exit(void* context) {
    UNUSED(context);
}

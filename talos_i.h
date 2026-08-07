#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "talos_icons.h" // generated from icons/ by fbt

#include "helpers/key_reader.h"
#include "helpers/tls_grade.h"
#include "helpers/tls_store.h"
#include "views/scan_view.h"
#include "views/result_view.h"
#include "scenes/talos_scene.h"

#define TALOS_VERSION "1.0"

typedef enum {
    TalosViewSubmenu,
    TalosViewScan,
    TalosViewResult,
    TalosViewSettings,
    TalosViewWidget,
} TalosViewId;

typedef enum {
    TalosCustomEventKeyRead = 100, /* a verdict is ready */
    TalosCustomEventRescan, /* user asked to grade another */
    TalosCustomEventReport, /* user opened the full breakdown */
} TalosCustomEvent;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    VariableItemList* var_item_list;
    /* The "Clear keyring" row, kept so its enter handler can report back into
     * the value column. The module exposes no way to look an item up by index. */
    VariableItem* clear_log_item;
    Widget* widget;
    ScanView* scan_view;
    ResultView* result_view;

    KeyReader* reader;
    TalosSettings settings;

    /* the current verdict */
    KeyCapture capture;
    TlsGrade grade;
    FuriString* decoded_fields; /* the firmware's own data dump, for the report */

    /* Ticks left in the closing flourish: the moment after a key answers, when
     * the scan trace clocks out the ROM it just read before the result appears.
     * Held here rather than in the scene state, which is already counting the
     * hold window. */
    uint8_t flourish;
} TalosApp;

/* feedback (defined in talos.c), all gated by settings */
void talos_notify_graded(TalosApp* app, const TlsGrade* grade);

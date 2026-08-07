#pragma once

#include <gui/view.h>

#include "../helpers/tls_grade.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ResultEventReport, /* OK - open the full breakdown */
    ResultEventRescan, /* Right - grade another key */
} ResultEvent;

typedef struct ResultView ResultView;
typedef void (*ResultViewCallback)(void* context, ResultEvent event);

ResultView* result_view_alloc(void);
void result_view_free(ResultView* v);
View* result_view_get_view(ResultView* v);
void result_view_set_callback(ResultView* v, ResultViewCallback cb, void* context);

void result_view_set_result(ResultView* v, const TlsGrade* grade, const TlsReading* reading);

#ifdef __cplusplus
}
#endif

#ifndef MUSIALIZER_UI_NOTICE_H_
#define MUSIALIZER_UI_NOTICE_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UI_NOTICE_CAPACITY 16u
#define UI_NOTICE_TITLE_CAPACITY 80u
#define UI_NOTICE_DETAIL_CAPACITY 320u
#define UI_NOTICE_PATH_CAPACITY 1025u

typedef enum { UI_NOTICE_INFO, UI_NOTICE_SUCCESS, UI_NOTICE_WARNING, UI_NOTICE_ERROR,
               UI_NOTICE_SEVERITY_COUNT } Ui_Notice_Severity;
typedef struct {
    uint64_t id;
    Ui_Notice_Severity severity;
    bool persistent;
    double remaining_seconds;
    char title[UI_NOTICE_TITLE_CAPACITY];
    char detail[UI_NOTICE_DETAIL_CAPACITY];
    char path[UI_NOTICE_PATH_CAPACITY];
} Ui_Notice;
typedef struct {
    Ui_Notice_Severity severity;
    bool persistent;
    double duration_seconds;
    const char *title;
    const char *detail;
    const char *path;
} Ui_Notice_Spec;
typedef struct {
    Ui_Notice notices[UI_NOTICE_CAPACITY];
    size_t count;
    uint64_t next_id;
    uint64_t revision;
    uint64_t evicted_count;
    uint64_t dropped_count;
} Ui_Notice_Queue;
typedef enum {
    UI_NOTICE_OK, UI_NOTICE_DROPPED, UI_NOTICE_ERROR_NULL, UI_NOTICE_ERROR_INVALID,
    UI_NOTICE_ERROR_STRING_TOO_LONG, UI_NOTICE_ERROR_NOT_FOUND, UI_NOTICE_ERROR_ID_EXHAUSTED
} Ui_Notice_Result;

void ui_notice_queue_init(Ui_Notice_Queue *queue);
Ui_Notice_Result ui_notice_push(Ui_Notice_Queue *queue, const Ui_Notice_Spec *spec,
                                uint64_t *notice_id);
Ui_Notice_Result ui_notice_dismiss(Ui_Notice_Queue *queue, uint64_t notice_id);
Ui_Notice_Result ui_notice_tick(Ui_Notice_Queue *queue, double delta_seconds);
void ui_notice_clear(Ui_Notice_Queue *queue);
const Ui_Notice *ui_notice_find(const Ui_Notice_Queue *queue, uint64_t notice_id);
bool ui_notice_is_actionable_failure(const Ui_Notice *notice);
const char *ui_notice_result_string(Ui_Notice_Result result);
const char *ui_notice_severity_label(Ui_Notice_Severity severity);
#endif

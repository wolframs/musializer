#include "ui_notice.h"
#include <math.h>
#include <string.h>

static void bump(Ui_Notice_Queue *q) { if (++q->revision == 0) q->revision = 1; }
static bool bounded(const char *s, size_t cap, bool empty)
{
    if (!s) return empty;
    const char *end = memchr(s, 0, cap);
    return end && (empty || end != s);
}
static void copy_string(char *out, const char *in)
{
    if (!in) out[0] = 0; else memcpy(out, in, strlen(in) + 1);
}
static void remove_at(Ui_Notice_Queue *q, size_t at)
{
    memmove(q->notices + at, q->notices + at + 1,
            (q->count - at - 1)*sizeof(q->notices[0]));
    memset(q->notices + --q->count, 0, sizeof(q->notices[0]));
}
static unsigned priority(Ui_Notice_Severity severity, bool persistent, bool path)
{
    if (severity == UI_NOTICE_ERROR && (persistent || path)) return 5;
    if (severity == UI_NOTICE_ERROR) return 4;
    if (severity == UI_NOTICE_WARNING && (persistent || path)) return 3;
    if (persistent || path) return 2;
    return severity == UI_NOTICE_WARNING ? 1 : 0;
}
bool ui_notice_is_actionable_failure(const Ui_Notice *n)
{
    return n && n->severity == UI_NOTICE_ERROR && (n->persistent || n->path[0]);
}
void ui_notice_queue_init(Ui_Notice_Queue *q)
{
    if (!q) return;
    memset(q, 0, sizeof(*q));
    q->next_id = q->revision = 1;
}
Ui_Notice_Result ui_notice_push(Ui_Notice_Queue *q, const Ui_Notice_Spec *s, uint64_t *out)
{
    if (out) *out = 0;
    if (!q || !s) return UI_NOTICE_ERROR_NULL;
    if (s->severity < 0 || s->severity >= UI_NOTICE_SEVERITY_COUNT ||
        !isfinite(s->duration_seconds) || (!s->persistent && s->duration_seconds <= 0) ||
        q->count > UI_NOTICE_CAPACITY) return UI_NOTICE_ERROR_INVALID;
    if (!bounded(s->title, UI_NOTICE_TITLE_CAPACITY, false))
        return s->title ? UI_NOTICE_ERROR_STRING_TOO_LONG : UI_NOTICE_ERROR_NULL;
    if (!bounded(s->detail, UI_NOTICE_DETAIL_CAPACITY, true) ||
        !bounded(s->path, UI_NOTICE_PATH_CAPACITY, true)) return UI_NOTICE_ERROR_STRING_TOO_LONG;
    if (!q->next_id) return UI_NOTICE_ERROR_ID_EXHAUSTED;
    unsigned incoming = priority(s->severity, s->persistent, s->path && s->path[0]);
    if (q->count == UI_NOTICE_CAPACITY) {
        size_t candidate = 0;
        unsigned lowest = priority(q->notices[0].severity, q->notices[0].persistent,
                                   q->notices[0].path[0]);
        for (size_t i = 1; i < q->count; ++i) {
            unsigned p = priority(q->notices[i].severity, q->notices[i].persistent,
                                  q->notices[i].path[0]);
            if (p < lowest) { lowest = p; candidate = i; }
        }
        if (lowest > incoming) { ++q->dropped_count; return UI_NOTICE_DROPPED; }
        remove_at(q, candidate); ++q->evicted_count;
    }
    Ui_Notice *n = q->notices + q->count++;
    memset(n, 0, sizeof(*n)); n->id = q->next_id;
    q->next_id = n->id == UINT64_MAX ? 0 : n->id + 1;
    n->severity = s->severity; n->persistent = s->persistent;
    n->remaining_seconds = s->persistent ? 0 : s->duration_seconds;
    copy_string(n->title, s->title); copy_string(n->detail, s->detail); copy_string(n->path, s->path);
    bump(q); if (out) *out = n->id; return UI_NOTICE_OK;
}
Ui_Notice_Result ui_notice_dismiss(Ui_Notice_Queue *q, uint64_t id)
{
    if (!q) return UI_NOTICE_ERROR_NULL;
    if (!id || q->count > UI_NOTICE_CAPACITY) return UI_NOTICE_ERROR_INVALID;
    for (size_t i = 0; i < q->count; ++i) if (q->notices[i].id == id) {
        remove_at(q, i); bump(q); return UI_NOTICE_OK;
    }
    return UI_NOTICE_ERROR_NOT_FOUND;
}
Ui_Notice_Result ui_notice_tick(Ui_Notice_Queue *q, double dt)
{
    if (!q) return UI_NOTICE_ERROR_NULL;
    if (!isfinite(dt) || dt < 0 || q->count > UI_NOTICE_CAPACITY) return UI_NOTICE_ERROR_INVALID;
    bool changed = false;
    for (size_t i = 0; i < q->count;) {
        if (!q->notices[i].persistent && (q->notices[i].remaining_seconds -= dt) <= 0) {
            remove_at(q, i); changed = true;
        } else ++i;
    }
    if (changed) bump(q);
    return UI_NOTICE_OK;
}
void ui_notice_clear(Ui_Notice_Queue *q)
{
    if (q && q->count) { memset(q->notices, 0, sizeof(q->notices)); q->count = 0; bump(q); }
}
const Ui_Notice *ui_notice_find(const Ui_Notice_Queue *q, uint64_t id)
{
    if (!q || !id || q->count > UI_NOTICE_CAPACITY) return NULL;
    for (size_t i = 0; i < q->count; ++i) if (q->notices[i].id == id) return q->notices + i;
    return NULL;
}
const char *ui_notice_result_string(Ui_Notice_Result r)
{
    static const char *names[] = {"ok", "notice dropped by overflow policy", "null argument",
        "invalid notice or queue", "notice string exceeds fixed capacity", "notice not found",
        "notice id space exhausted"};
    return r >= 0 && (size_t)r < sizeof(names)/sizeof(names[0]) ? names[r] : "unknown notice result";
}

const char *ui_notice_severity_label(Ui_Notice_Severity severity)
{
    switch (severity) {
    case UI_NOTICE_INFO: return "INFO";
    case UI_NOTICE_SUCCESS: return "DONE";
    case UI_NOTICE_WARNING: return "WARNING";
    case UI_NOTICE_ERROR: return "ERROR";
    case UI_NOTICE_SEVERITY_COUNT: break;
    }
    return "NOTICE";
}

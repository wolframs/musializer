#include "test_support.h"
#include "ui_notice.h"
#include <math.h>
#include <string.h>

static Ui_Notice_Spec timed(Ui_Notice_Severity s, const char *title)
{ return (Ui_Notice_Spec){.severity=s, .duration_seconds=2, .title=title}; }
static Ui_Notice_Spec failure(const char *title)
{ return (Ui_Notice_Spec){.severity=UI_NOTICE_ERROR, .persistent=true,
    .title=title, .detail="Previous state preserved", .path="/tmp/job.log"}; }

TEST(ui_notice_expiry_preserves_persistent_failures)
{
    Ui_Notice_Queue q; ui_notice_queue_init(&q);
    Ui_Notice_Spec transient = timed(UI_NOTICE_SUCCESS, "Saved");
    Ui_Notice_Spec error = failure("Render failed");
    uint64_t transient_id, error_id;
    REQUIRE_TRUE(ui_notice_push(&q, &transient, &transient_id) == UI_NOTICE_OK);
    REQUIRE_TRUE(ui_notice_push(&q, &error, &error_id) == UI_NOTICE_OK);
    REQUIRE_TRUE(ui_notice_tick(&q, 2) == UI_NOTICE_OK);
    EXPECT_TRUE(ui_notice_find(&q, transient_id) == NULL);
    const Ui_Notice *kept = ui_notice_find(&q, error_id);
    REQUIRE_TRUE(kept != NULL);
    EXPECT_TRUE(ui_notice_is_actionable_failure(kept));
    EXPECT_TRUE(strcmp(kept->detail, "Previous state preserved") == 0);
}

TEST(ui_notice_dismiss_and_clear_advance_revision)
{
    Ui_Notice_Queue q; ui_notice_queue_init(&q);
    Ui_Notice_Spec item = timed(UI_NOTICE_INFO, "One"); uint64_t id;
    REQUIRE_TRUE(ui_notice_push(&q, &item, &id) == UI_NOTICE_OK);
    uint64_t revision = q.revision;
    REQUIRE_TRUE(ui_notice_dismiss(&q, id) == UI_NOTICE_OK);
    EXPECT_EQ_SIZE(q.count, 0); EXPECT_TRUE(q.revision != revision);
    EXPECT_TRUE(ui_notice_dismiss(&q, id) == UI_NOTICE_ERROR_NOT_FOUND);
    REQUIRE_TRUE(ui_notice_push(&q, &item, NULL) == UI_NOTICE_OK);
    ui_notice_clear(&q); EXPECT_EQ_SIZE(q.count, 0);
}

TEST(ui_notice_invalid_input_is_atomic)
{
    Ui_Notice_Queue q; ui_notice_queue_init(&q); uint64_t revision = q.revision;
    Ui_Notice_Spec item = timed(UI_NOTICE_INFO, "Good"); item.duration_seconds = NAN;
    EXPECT_TRUE(ui_notice_push(&q, &item, NULL) == UI_NOTICE_ERROR_INVALID);
    item.duration_seconds = 1; item.title = "";
    EXPECT_TRUE(ui_notice_push(&q, &item, NULL) == UI_NOTICE_ERROR_STRING_TOO_LONG);
    EXPECT_EQ_SIZE(q.count, 0); EXPECT_EQ_U64(q.revision, revision);
    EXPECT_TRUE(ui_notice_tick(&q, -1) == UI_NOTICE_ERROR_INVALID);
}

TEST(ui_notice_overflow_eviction_prefers_transient_chatter)
{
    Ui_Notice_Queue q; ui_notice_queue_init(&q);
    Ui_Notice_Spec error = failure("Important"); uint64_t error_id;
    REQUIRE_TRUE(ui_notice_push(&q, &error, &error_id) == UI_NOTICE_OK);
    for (size_t i = 1; i < UI_NOTICE_CAPACITY; ++i) {
        Ui_Notice_Spec info = timed(UI_NOTICE_INFO, "Update");
        REQUIRE_TRUE(ui_notice_push(&q, &info, NULL) == UI_NOTICE_OK);
    }
    Ui_Notice_Spec newest = timed(UI_NOTICE_SUCCESS, "Done");
    REQUIRE_TRUE(ui_notice_push(&q, &newest, NULL) == UI_NOTICE_OK);
    EXPECT_TRUE(ui_notice_find(&q, error_id) != NULL);
    EXPECT_EQ_U64(q.evicted_count, 1); EXPECT_EQ_U64(q.dropped_count, 0);
}

TEST(ui_notice_overflow_drops_low_priority_before_failures)
{
    Ui_Notice_Queue q; ui_notice_queue_init(&q);
    for (size_t i = 0; i < UI_NOTICE_CAPACITY; ++i) {
        Ui_Notice_Spec item = failure("Failure");
        REQUIRE_TRUE(ui_notice_push(&q, &item, NULL) == UI_NOTICE_OK);
    }
    uint64_t oldest = q.notices[0].id;
    Ui_Notice_Spec info = timed(UI_NOTICE_INFO, "Routine");
    EXPECT_TRUE(ui_notice_push(&q, &info, NULL) == UI_NOTICE_DROPPED);
    EXPECT_TRUE(ui_notice_find(&q, oldest) != NULL); EXPECT_EQ_U64(q.dropped_count, 1);
}

TEST(ui_notice_overflow_preserves_most_recent_actionable_failures)
{
    Ui_Notice_Queue q; ui_notice_queue_init(&q);
    for (size_t i = 0; i < UI_NOTICE_CAPACITY; ++i) {
        Ui_Notice_Spec item = failure("Failure");
        REQUIRE_TRUE(ui_notice_push(&q, &item, NULL) == UI_NOTICE_OK);
    }
    uint64_t oldest = q.notices[0].id, newest_id;
    Ui_Notice_Spec newest = failure("Newest failure");
    REQUIRE_TRUE(ui_notice_push(&q, &newest, &newest_id) == UI_NOTICE_OK);
    EXPECT_TRUE(ui_notice_find(&q, oldest) == NULL);
    EXPECT_TRUE(ui_notice_find(&q, newest_id) != NULL); EXPECT_EQ_U64(q.evicted_count, 1);
}

TEST(ui_notice_rejects_unterminated_bounded_strings)
{
    Ui_Notice_Queue q; ui_notice_queue_init(&q);
    char title[UI_NOTICE_TITLE_CAPACITY]; memset(title, 'x', sizeof(title));
    Ui_Notice_Spec item = timed(UI_NOTICE_INFO, title);
    EXPECT_TRUE(ui_notice_push(&q, &item, NULL) == UI_NOTICE_ERROR_STRING_TOO_LONG);
    EXPECT_EQ_SIZE(q.count, 0);
}

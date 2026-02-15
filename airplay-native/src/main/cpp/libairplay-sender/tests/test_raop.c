#include <stdio.h>
#include <string.h>
#include "../src/core/raop.h"

static void dummy_state_cb(raop_session_t *s, raop_state_t state, void *ud)
{
    (void)s; (void)state; (void)ud;
}

static int test_session_new_valid(void)
{
    raop_session_t *s = raop_session_new("127.0.0.1", 7000, dummy_state_cb, NULL);
    if (!s) return 1;
    raop_session_free(s);
    return 0;
}

static int test_session_initial_state_is_stopped(void)
{
    raop_session_t *s = raop_session_new("127.0.0.1", 7000, dummy_state_cb, NULL);
    if (!s) return 1;
    int ok = raop_session_get_state(s) == RAOP_STATE_STOPPED;
    raop_session_free(s);
    return ok ? 0 : 1;
}

static int test_session_initial_volume(void)
{
    raop_session_t *s = raop_session_new("127.0.0.1", 7000, dummy_state_cb, NULL);
    if (!s) return 1;
    int vol = raop_session_get_volume_pct(s);
    raop_session_free(s);
    return (vol >= 0 && vol <= 100) ? 0 : 1;
}

static int test_session_free_null_is_safe(void)
{
    raop_session_free(NULL);
    return 0;
}

static int test_session_stop_when_not_running(void)
{
    raop_session_t *s = raop_session_new("127.0.0.1", 7000, dummy_state_cb, NULL);
    if (!s) return 1;
    int ret = raop_session_stop(s);
    raop_session_free(s);
    return (ret == 0) ? 0 : 1;
}

static int test_session_pause_rejects_wrong_state(void)
{
    raop_session_t *s = raop_session_new("127.0.0.1", 7000, dummy_state_cb, NULL);
    if (!s) return 1;
    int ret = raop_session_pause(s);
    raop_session_free(s);
    return (ret < 0) ? 0 : 1;
}

static int test_session_resume_rejects_wrong_state(void)
{
    raop_session_t *s = raop_session_new("127.0.0.1", 7000, dummy_state_cb, NULL);
    if (!s) return 1;
    int ret = raop_session_resume(s);
    raop_session_free(s);
    return (ret < 0) ? 0 : 1;
}

static int test_session_seek_rejects_wrong_state(void)
{
    raop_session_t *s = raop_session_new("127.0.0.1", 7000, dummy_state_cb, NULL);
    if (!s) return 1;
    int ret = raop_session_seek(s, 5000);
    raop_session_free(s);
    return (ret < 0) ? 0 : 1;
}

static int test_session_set_volume_rejects_wrong_state(void)
{
    raop_session_t *s = raop_session_new("127.0.0.1", 7000, dummy_state_cb, NULL);
    if (!s) return 1;
    int ret = raop_session_set_volume(s, 50);
    raop_session_free(s);
    return (ret < 0) ? 0 : 1;
}

static int test_session_position_zero_when_not_streaming(void)
{
    raop_session_t *s = raop_session_new("127.0.0.1", 7000, dummy_state_cb, NULL);
    if (!s) return 1;
    uint32_t pos = raop_session_get_position_ms(s);
    raop_session_free(s);
    return (pos == 0) ? 0 : 1;
}

static int test_push_pcm_rejects_when_not_push_mode(void)
{
    raop_session_t *s = raop_session_new("127.0.0.1", 7000, dummy_state_cb, NULL);
    if (!s) return 1;
    int16_t pcm[704] = {0};
    int ret = raop_session_push_pcm(s, pcm, 352);
    raop_session_free(s);
    return (ret < 0) ? 0 : 1;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } tests[] = {
        { "session_new_valid", test_session_new_valid },
        { "session_initial_state_is_stopped", test_session_initial_state_is_stopped },
        { "session_initial_volume", test_session_initial_volume },
        { "session_free_null_is_safe", test_session_free_null_is_safe },
        { "session_stop_when_not_running", test_session_stop_when_not_running },
        { "session_pause_rejects_wrong_state", test_session_pause_rejects_wrong_state },
        { "session_resume_rejects_wrong_state", test_session_resume_rejects_wrong_state },
        { "session_seek_rejects_wrong_state", test_session_seek_rejects_wrong_state },
        { "session_set_volume_rejects_wrong_state", test_session_set_volume_rejects_wrong_state },
        { "session_position_zero_when_not_streaming", test_session_position_zero_when_not_streaming },
        { "push_pcm_rejects_when_not_push_mode", test_push_pcm_rejects_when_not_push_mode },
    };

    int n = sizeof(tests) / sizeof(tests[0]);
    int failures = 0;
    for (int i = 0; i < n; i++) {
        int result = tests[i].fn();
        printf("  %s %s\n", result == 0 ? "\u2713" : "\u2717", tests[i].name);
        failures += result;
    }
    printf("\n%d/%d passed\n", n - failures, n);
    return failures;
}

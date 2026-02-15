#ifndef RAOP_H
#define RAOP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct raop_session raop_session_t;

typedef enum {
    RAOP_STATE_STOPPED = 0,
    RAOP_STATE_CONNECTED,
    RAOP_STATE_STREAMING,
    RAOP_STATE_PAUSED,
    RAOP_STATE_FAILED,
    RAOP_STATE_IDLE
} raop_state_t;

typedef struct raop_metadata {
    const char *title;
    const char *artist;
    const char *album;
    const char *genre;
    uint32_t duration_ms;
    uint32_t track_number;
    uint32_t disc_number;
} raop_metadata_t;

typedef void (*raop_state_cb)(raop_session_t *session, raop_state_t state, void *user_data);

raop_session_t *raop_session_new(const char *host, uint16_t port,
                                 raop_state_cb state_cb, void *user_data);
void raop_session_free(raop_session_t *session);

int raop_session_connect(raop_session_t *session);
int raop_session_play(raop_session_t *session, const char *path, uint32_t start_ms);
int raop_session_stop(raop_session_t *session);
int raop_session_pause(raop_session_t *session);
int raop_session_resume(raop_session_t *session);
int raop_session_seek(raop_session_t *session, uint32_t position_ms);
int raop_session_set_volume(raop_session_t *session, int volume_pct);
int raop_session_set_metadata(raop_session_t *session, const raop_metadata_t *meta);
int raop_session_set_artwork(raop_session_t *session, const uint8_t *data, size_t len, int is_png);

raop_state_t raop_session_get_state(const raop_session_t *session);
uint32_t raop_session_get_position_ms(const raop_session_t *session);
uint32_t raop_session_get_duration_ms(const raop_session_t *session);
int raop_session_get_volume_pct(const raop_session_t *session);
void raop_session_start_push(raop_session_t *session);
int raop_session_push_pcm(raop_session_t *session, const int16_t *pcm, int samples);
void raop_session_flush_push_buffer(raop_session_t *session);

#ifdef __cplusplus
}
#endif

#endif

/*
 * AirPlay 1 (RAOP) session implementation
 *
 * Protocol flow (matching OwnTone's behavior with AirTunes/366.0 devices):
 *   OPTIONS → auth-setup(Curve25519) → ANNOUNCE(SDP) →
 *   SETUP(Transport header) → RECORD → SET_PARAMETER(volume) →
 *   RTP audio (unencrypted ALAC/UDP) → TEARDOWN
 */

#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>

#ifdef __ANDROID__
#include <android/log.h>
#define RAOP_TAG "RAOP"
#define RAOP_LOG(...)  __android_log_print(ANDROID_LOG_DEBUG, RAOP_TAG, __VA_ARGS__)
#define RAOP_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  RAOP_TAG, __VA_ARGS__)
#define RAOP_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  RAOP_TAG, __VA_ARGS__)
#define RAOP_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, RAOP_TAG, __VA_ARGS__)
#else
#define RAOP_LOG(...)  do { printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while(0)
#define RAOP_LOGI(...) RAOP_LOG(__VA_ARGS__)
#define RAOP_LOGW(...) RAOP_LOG(__VA_ARGS__)
#define RAOP_LOGE(...) RAOP_LOG(__VA_ARGS__)
#endif
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <netinet/tcp.h>

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>

#include "raop.h"
#include "rtp_stream.h"

#define SAMPLES_PER_PACKET 352
#define SAMPLE_RATE        44100
#define CHANNELS           2
#define ALAC_HEADER_LEN    3
#define BODY_LEN           (SAMPLES_PER_PACKET * CHANNELS * 2)
#define RING_BUF_SECONDS 2
#define RING_BUF_SAMPLES (SAMPLE_RATE * RING_BUF_SECONDS)  /* in stereo sample-pairs */

struct alac_encoder {
    const AVCodec *codec;
    AVCodecContext *ctx;
    AVFrame *frame;
    AVPacket *pkt;
};

struct audio_decoder {
    AVFormatContext *fmt_ctx;
    AVCodecContext  *dec_ctx;
    SwrContext      *swr;
    int              stream_idx;
    AVFrame         *frame;
    AVPacket        *pkt;
    int16_t          resample_buf[8192 * CHANNELS];
    int              buf_samples;
    int              buf_offset;
    uint32_t         duration_ms;
    uint32_t         total_samples;
    char             title[256];
    char             artist[256];
    char             album[256];
    char             genre[256];
};

enum raop_cmd_type {
    RAOP_CMD_NONE = 0,
    RAOP_CMD_VOLUME,
    RAOP_CMD_METADATA,
    RAOP_CMD_ARTWORK,
    RAOP_CMD_PROGRESS,
    RAOP_CMD_PAUSE,
    RAOP_CMD_RESUME,
    RAOP_CMD_SEEK,
    RAOP_CMD_PLAY,
};

struct raop_cmd {
    enum raop_cmd_type type;
    double volume_db;
    raop_metadata_t metadata;
    uint8_t *artwork_data;
    size_t artwork_len;
    int artwork_is_png;
    uint32_t seek_position_ms;
    uint32_t progress_start, progress_cur, progress_end;
    char *play_path;
};

static void raop_cmd_free_metadata(struct raop_cmd *cmd);

static struct alac_encoder *alac_encoder_create(void)
{
    struct alac_encoder *enc = calloc(1, sizeof(*enc));
    if (!enc) return NULL;

    enc->codec = avcodec_find_encoder(AV_CODEC_ID_ALAC);
    if (!enc->codec) { free(enc); return NULL; }

    enc->ctx = avcodec_alloc_context3(enc->codec);
    if (!enc->ctx) { free(enc); return NULL; }

    enc->ctx->sample_fmt = AV_SAMPLE_FMT_S16P;
    enc->ctx->sample_rate = SAMPLE_RATE;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    av_channel_layout_copy(&enc->ctx->ch_layout, &stereo);
    enc->ctx->frame_size = SAMPLES_PER_PACKET;

    if (avcodec_open2(enc->ctx, enc->codec, NULL) < 0) {
        avcodec_free_context(&enc->ctx);
        free(enc);
        return NULL;
    }

    /* ffmpeg ALAC encoder defaults to frame_size=4096 and rejects 352-sample
     * frames in ffmpeg 6+. Override after open, matching OwnTone's workaround. */
    enc->ctx->frame_size = SAMPLES_PER_PACKET;

    enc->frame = av_frame_alloc();
    enc->pkt = av_packet_alloc();
    if (!enc->frame || !enc->pkt) {
        av_frame_free(&enc->frame);
        av_packet_free(&enc->pkt);
        avcodec_free_context(&enc->ctx);
        free(enc);
        return NULL;
    }

    enc->frame->format = AV_SAMPLE_FMT_S16P;
    enc->frame->sample_rate = SAMPLE_RATE;
    av_channel_layout_copy(&enc->frame->ch_layout, &stereo);
    enc->frame->nb_samples = SAMPLES_PER_PACKET;

    if (av_frame_get_buffer(enc->frame, 0) < 0) {
        av_frame_free(&enc->frame);
        av_packet_free(&enc->pkt);
        avcodec_free_context(&enc->ctx);
        free(enc);
        return NULL;
    }

    return enc;
}

static void alac_encoder_destroy(struct alac_encoder *enc)
{
    if (!enc) return;
    av_frame_free(&enc->frame);
    av_packet_free(&enc->pkt);
    avcodec_free_context(&enc->ctx);
    free(enc);
}

static int alac_encode_compressed(struct alac_encoder *enc, uint8_t *dst, size_t dst_size,
                                  const int16_t *pcm_interleaved, int nsamples)
{
    if (av_frame_make_writable(enc->frame) < 0) return -1;

    int16_t *left = (int16_t *)enc->frame->data[0];
    int16_t *right = (int16_t *)enc->frame->data[1];
    for (int i = 0; i < nsamples; i++) {
        left[i] = pcm_interleaved[i * 2];
        right[i] = pcm_interleaved[i * 2 + 1];
    }
    enc->frame->nb_samples = nsamples;

    int ret = avcodec_send_frame(enc->ctx, enc->frame);
    if (ret < 0) return -1;

    ret = avcodec_receive_packet(enc->ctx, enc->pkt);
    if (ret < 0) return -1;

    if ((size_t)enc->pkt->size > dst_size) {
        av_packet_unref(enc->pkt);
        return -1;
    }

    memcpy(dst, enc->pkt->data, enc->pkt->size);
    int size = enc->pkt->size;
    av_packet_unref(enc->pkt);
    return size;
}

static const uint8_t auth_setup_pubkey[] =
  "\x59\x02\xed\xe9\x0d\x4e\xf2\xbd\x4c\xb6\x8a\x63\x30\x03\x82\x07"
  "\xa9\x4d\xbd\x50\xd8\xaa\x46\x5b\x5d\x8c\x01\x2a\x0c\x7e\x1d\x4e";

#define COMMON_HEADERS \
    "User-Agent: AirPlay/320.20\r\n" \
    "Client-Instance: AABBCCDDEEFF1122\r\n" \
    "DACP-ID: AABBCCDDEEFF1122\r\n" \
    "Active-Remote: 1234567890\r\n" \
    "X-Apple-Client-Name: libairplay-sender\r\n"

struct raop_session {
    char *host;
    int port;
    raop_state_t state;
    int sockfd;
    pthread_t thread;
    int running;
    raop_state_cb state_cb;
    void *user_data;

    rtp_stream_t *rtp;
    uint32_t session_id;
    char session_url[256];
    char rtsp_session[64];
    int cseq;

    int timing_fd;
    uint16_t timing_port;
    pthread_t timing_thread;
    int timing_running;

    char *audio_file;
    struct alac_encoder *alac_enc;

    struct raop_cmd pending_cmd;
    pthread_mutex_t cmd_mutex;
    int paused;
    uint32_t total_samples;
    uint32_t start_rtptime;
    double current_volume_db;
    uint32_t base_rtptime;    /* RTP timestamp at RECORD, used as start_rtptime base */
    /* Push-mode PCM ring buffer */
    int push_mode;
    int16_t *pcm_ring;
    int rb_read;
    int rb_write;
    int rb_capacity;          /* total samples (stereo pairs) capacity */
    pthread_mutex_t rb_mutex;
    pthread_cond_t rb_cond;   /* signal consumer when data arrives */
};

static void enable_tcp_keepalive(int fd)
{
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
    int idle = 5;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    int intvl = 5;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    int cnt = 3;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
}

static int check_rtsp_socket(int fd)
{
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int ret = poll(&pfd, 1, 0);
    if (ret > 0 && (pfd.revents & (POLLHUP | POLLERR)))
        return -1;
    if (ret > 0 && (pfd.revents & POLLIN)) {
        char peek;
        ssize_t n = recv(fd, &peek, 1, MSG_PEEK | MSG_DONTWAIT);
        if (n == 0) return -1;
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return -1;
    }
    return 0;
}

static int send_raw(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0) return -1;
        p += n; len -= n;
    }
    return 0;
}

static int recv_full(int fd, void *buf, size_t len)
{
    uint8_t *p = buf;
    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);
        if (n <= 0) return -1;
        p += n; len -= n;
    }
    return 0;
}

static int rtsp_send(int fd, const char *headers, const uint8_t *body, size_t body_len)
{
    if (send_raw(fd, headers, strlen(headers)) < 0) return -1;
    if (body && body_len > 0)
        if (send_raw(fd, body, body_len) < 0) return -1;
    return 0;
}

static int rtsp_recv(int fd, char *hdrbuf, size_t hdrbuf_sz, int *status,
                     uint8_t **body_out, size_t *body_len_out)
{
    size_t total = 0;
    char *hdr_end = NULL;

    struct timeval tv = { .tv_sec = 10 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (total < hdrbuf_sz - 1) {
        ssize_t n = recv(fd, hdrbuf + total, hdrbuf_sz - 1 - total, 0);
        if (n <= 0) return -1;
        total += n;
        hdrbuf[total] = '\0';
        if ((hdr_end = strstr(hdrbuf, "\r\n\r\n"))) break;
    }
    if (!hdr_end) return -1;

    sscanf(hdrbuf, "RTSP/1.0 %d", status);

    size_t hdr_size = (hdr_end - hdrbuf) + 4;
    int cl = 0;
    const char *p = strstr(hdrbuf, "Content-Length:");
    if (!p) p = strstr(hdrbuf, "content-length:");
    if (p) cl = atoi(p + 15);

    size_t body_got = total - hdr_size;
    if (body_out && cl > 0) {
        *body_out = malloc(cl);
        if (body_got > 0) memcpy(*body_out, hdr_end + 4, body_got);
        if ((int)body_got < cl)
            recv_full(fd, *body_out + body_got, cl - body_got);
        if (body_len_out) *body_len_out = cl;
    } else if (cl > 0 && !body_out) {
        char drain[1024];
        size_t remaining = cl - body_got;
        while (remaining > 0) {
            size_t chunk = remaining < sizeof(drain) ? remaining : sizeof(drain);
            ssize_t n = recv(fd, drain, chunk, 0);
            if (n <= 0) break;
            remaining -= n;
        }
    }

    tv.tv_sec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return 0;
}

static int do_options(int fd, int *cseq)
{
    char hdr[512];
    snprintf(hdr, sizeof(hdr),
        "OPTIONS * RTSP/1.0\r\nCSeq: %d\r\n"
        COMMON_HEADERS "\r\n", (*cseq)++);

    if (rtsp_send(fd, hdr, NULL, 0) < 0) return -1;

    char resp[2048];
    int status = 0;
    if (rtsp_recv(fd, resp, sizeof(resp), &status, NULL, NULL) < 0) return -1;
    RAOP_LOG("OPTIONS => %d", status);
    return (status == 200) ? 0 : -1;
}

static int do_auth_setup(int fd, int *cseq)
{
    uint8_t body[33];
    body[0] = 0x01;
    memcpy(body + 1, auth_setup_pubkey, 32);

    char hdr[512];
    snprintf(hdr, sizeof(hdr),
        "POST /auth-setup RTSP/1.0\r\nCSeq: %d\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: 33\r\n"
        COMMON_HEADERS "\r\n", (*cseq)++);

    if (rtsp_send(fd, hdr, body, 33) < 0) return -1;

    char resp[2048];
    int status = 0;
    uint8_t *resp_body = NULL;
    size_t resp_body_len = 0;
    if (rtsp_recv(fd, resp, sizeof(resp), &status, &resp_body, &resp_body_len) < 0) return -1;
    RAOP_LOG("auth-setup => %d", status);
    free(resp_body);
    return (status == 200) ? 0 : -1;
}

static int do_announce(int fd, int *cseq, const char *session_url,
                       const char *local_ip, const char *remote_host,
                       uint32_t session_id)
{
    char sdp[512];
    int sdp_len = snprintf(sdp, sizeof(sdp),
        "v=0\r\n"
        "o=iTunes %u 0 IN IP4 %s\r\n"
        "s=iTunes\r\n"
        "c=IN IP4 %s\r\n"
        "t=0 0\r\n"
        "m=audio 0 RTP/AVP 96\r\n"
        "a=rtpmap:96 AppleLossless\r\n"
        "a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100\r\n",
        session_id, local_ip, remote_host);

    char hdr[1024];
    snprintf(hdr, sizeof(hdr),
        "ANNOUNCE %s RTSP/1.0\r\nCSeq: %d\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: %d\r\n"
        COMMON_HEADERS "\r\n",
        session_url, (*cseq)++, sdp_len);

    if (rtsp_send(fd, hdr, (const uint8_t *)sdp, sdp_len) < 0) return -1;

    char resp[2048];
    int status = 0;
    if (rtsp_recv(fd, resp, sizeof(resp), &status, NULL, NULL) < 0) return -1;
    RAOP_LOG("ANNOUNCE => %d", status);
    return (status == 200) ? 0 : -1;
}

static int raop_parse_transport(const char *transport,
                         uint16_t *server_port,
                         uint16_t *control_port,
                         uint16_t *timing_port)
{
    const char *p;
    if (server_port) {
        p = strstr(transport, "server_port=");
        if (p) *server_port = (uint16_t)atoi(p + 12);
    }
    if (control_port) {
        p = strstr(transport, "control_port=");
        if (p) *control_port = (uint16_t)atoi(p + 13);
    }
    if (timing_port) {
        p = strstr(transport, "timing_port=");
        if (p) *timing_port = (uint16_t)atoi(p + 12);
    }
    return 0;
}

void raop_session_start_push(raop_session_t *s)
{
    if (!s) return;

    pthread_mutex_lock(&s->rb_mutex);
    if (!s->pcm_ring) {
        s->rb_capacity = RING_BUF_SAMPLES;
        s->pcm_ring = calloc(s->rb_capacity * CHANNELS, sizeof(int16_t));
    }
    int was_push = s->push_mode;
    if (!was_push) {
        s->rb_read = 0;
        s->rb_write = 0;
    }
    s->push_mode = 1;
    pthread_mutex_unlock(&s->rb_mutex);

    pthread_mutex_lock(&s->cmd_mutex);
    free(s->pending_cmd.play_path);
    s->pending_cmd.type = RAOP_CMD_PLAY;
    s->pending_cmd.play_path = NULL;
    s->pending_cmd.seek_position_ms = 0;
    pthread_mutex_unlock(&s->cmd_mutex);

    RAOP_LOG("Push mode %s (ring buffer %d samples)",
             was_push ? "track-switch" : "started", s->rb_capacity);
}

int raop_session_push_pcm(raop_session_t *s, const int16_t *pcm, int samples)
{
    if (!s || !s->push_mode || !s->pcm_ring || !pcm || samples <= 0) return -1;

    pthread_mutex_lock(&s->rb_mutex);
    for (int i = 0; i < samples; i++) {
        int next_write = (s->rb_write + 1) % s->rb_capacity;
        while (next_write == s->rb_read && s->push_mode && s->running) {
            /* buffer full — block until consumer reads */
            pthread_cond_signal(&s->rb_cond);
            struct timespec abstime;
            clock_gettime(CLOCK_REALTIME, &abstime);
            abstime.tv_nsec += 10000000; /* 10ms */
            if (abstime.tv_nsec >= 1000000000) {
                abstime.tv_sec++;
                abstime.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&s->rb_cond, &s->rb_mutex, &abstime);
            next_write = (s->rb_write + 1) % s->rb_capacity;
        }
        if (!s->push_mode || !s->running) break;
        s->pcm_ring[s->rb_write * CHANNELS]     = pcm[i * CHANNELS];
        s->pcm_ring[s->rb_write * CHANNELS + 1] = pcm[i * CHANNELS + 1];
        s->rb_write = next_write;
    }
    pthread_cond_signal(&s->rb_cond);
    pthread_mutex_unlock(&s->rb_mutex);
    return samples;
}

void raop_session_flush_push_buffer(raop_session_t *s)
{
    if (!s) return;
    pthread_mutex_lock(&s->rb_mutex);
    s->rb_read = 0;
    s->rb_write = 0;
    pthread_cond_broadcast(&s->rb_cond);
    pthread_mutex_unlock(&s->rb_mutex);
    RAOP_LOG("Push buffer flushed");
}

static int do_setup(int fd, int *cseq, const char *session_url,
                    uint16_t our_control_port, uint16_t our_timing_port,
                    uint16_t *out_server_port, uint16_t *out_control_port,
                    uint16_t *out_timing_port, char *session_out, size_t session_sz)
{
    char hdr[1024];
    snprintf(hdr, sizeof(hdr),
        "SETUP %s RTSP/1.0\r\nCSeq: %d\r\n"
        "Transport: RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;"
        "control_port=%u;timing_port=%u\r\n"
        COMMON_HEADERS "\r\n",
        session_url, (*cseq)++, our_control_port, our_timing_port);

    if (rtsp_send(fd, hdr, NULL, 0) < 0) return -1;

    char resp[4096];
    int status = 0;
    if (rtsp_recv(fd, resp, sizeof(resp), &status, NULL, NULL) < 0) return -1;
    RAOP_LOG("SETUP => %d", status);

    if (status != 200) return -1;

    const char *transport = strstr(resp, "Transport:");
    if (!transport) transport = strstr(resp, "transport:");
    if (transport)
        raop_parse_transport(transport, out_server_port, out_control_port, out_timing_port);

    const char *sess = strstr(resp, "Session:");
    if (!sess) sess = strstr(resp, "session:");
    if (sess) {
        sess += 8;
        while (*sess == ' ') sess++;
        int i = 0;
        while (sess[i] && sess[i] != '\r' && sess[i] != '\n' && i < (int)session_sz - 1) {
            session_out[i] = sess[i];
            i++;
        }
        session_out[i] = '\0';
    }

    return 0;
}

static int do_record(int fd, int *cseq, const char *session_url,
                     const char *session, uint16_t initial_seq, uint32_t initial_rtptime)
{
    char hdr[1024];
    snprintf(hdr, sizeof(hdr),
        "RECORD %s RTSP/1.0\r\nCSeq: %d\r\n"
        "Session: %s\r\n"
        "Range: npt=0-\r\n"
        "RTP-Info: seq=%u;rtptime=%u\r\n"
        COMMON_HEADERS "\r\n",
        session_url, (*cseq)++, session, initial_seq, initial_rtptime);

    if (rtsp_send(fd, hdr, NULL, 0) < 0) return -1;

    char resp[2048];
    int status = 0;
    if (rtsp_recv(fd, resp, sizeof(resp), &status, NULL, NULL) < 0) return -1;
    RAOP_LOG("RECORD => %d", status);
    return (status == 200) ? 0 : -1;
}

static int do_get_volume(int fd, int *cseq, const char *session_url,
                         const char *session, double *volume_db_out)
{
    const char *body = "volume\r\n";
    int body_len = (int)strlen(body);

    char hdr[1024];
    snprintf(hdr, sizeof(hdr),
        "GET_PARAMETER %s RTSP/1.0\r\nCSeq: %d\r\n"
        "Session: %s\r\n"
        "Content-Type: text/parameters\r\n"
        "Content-Length: %d\r\n"
        COMMON_HEADERS "\r\n",
        session_url, (*cseq)++, session, body_len);

    if (rtsp_send(fd, hdr, (const uint8_t *)body, body_len) < 0) return -1;

    char resp[2048];
    int status = 0;
    uint8_t *resp_body = NULL;
    size_t resp_body_len = 0;
    if (rtsp_recv(fd, resp, sizeof(resp), &status, &resp_body, &resp_body_len) < 0) return -1;

    int ret = -1;
    if (status == 200 && resp_body && resp_body_len > 0) {
        const char *vp = strstr((const char *)resp_body, "volume:");
        if (vp) {
            double v = atof(vp + 7);
            if (v >= -144.0 && v <= 0.0) {
                *volume_db_out = v;
                ret = 0;
            }
        }
    }
    free(resp_body);
    RAOP_LOG("GET_PARAMETER(volume) => %d, ret=%d", status, ret);
    return ret;
}

static int do_set_volume(int fd, int *cseq, const char *session_url,
                         const char *session, double volume_db)
{
    char body[64];
    int body_len = snprintf(body, sizeof(body), "volume: %f\r\n", volume_db);

    char hdr[1024];
    snprintf(hdr, sizeof(hdr),
        "SET_PARAMETER %s RTSP/1.0\r\nCSeq: %d\r\n"
        "Session: %s\r\n"
        "Content-Type: text/parameters\r\n"
        "Content-Length: %d\r\n"
        COMMON_HEADERS "\r\n",
        session_url, (*cseq)++, session, body_len);

    if (rtsp_send(fd, hdr, (const uint8_t *)body, body_len) < 0) return -1;

    char resp[2048];
    int status = 0;
    if (rtsp_recv(fd, resp, sizeof(resp), &status, NULL, NULL) < 0) return -1;
    RAOP_LOG("SET_PARAMETER(volume) => %d", status);
    return (status == 200) ? 0 : -1;
}

static int do_set_progress(int fd, int *cseq, const char *session_url,
                           const char *session, uint32_t start, uint32_t cur, uint32_t end)
{
    char body[128];
    int body_len = snprintf(body, sizeof(body), "progress: %u/%u/%u\r\n", start, cur, end);

    char hdr[1024];
    snprintf(hdr, sizeof(hdr),
        "SET_PARAMETER %s RTSP/1.0\r\nCSeq: %d\r\n"
        "Session: %s\r\n"
        "Content-Type: text/parameters\r\n"
        "Content-Length: %d\r\n"
        COMMON_HEADERS "\r\n",
        session_url, (*cseq)++, session, body_len);

    if (rtsp_send(fd, hdr, (const uint8_t *)body, body_len) < 0) return -1;

    char resp[2048];
    int status = 0;
    if (rtsp_recv(fd, resp, sizeof(resp), &status, NULL, NULL) < 0) return -1;
    RAOP_LOG("SET_PARAMETER(progress) => %d", status);
    return (status == 200) ? 0 : -1;
}

static void dmap_add_byte(uint8_t **p, const char tag[4], uint8_t val)
{
    memcpy(*p, tag, 4); *p += 4;
    uint32_t len = htonl(1); memcpy(*p, &len, 4); *p += 4;
    **p = val; (*p)++;
}

static void dmap_add_short(uint8_t **p, const char tag[4], uint16_t val)
{
    memcpy(*p, tag, 4); *p += 4;
    uint32_t len = htonl(2); memcpy(*p, &len, 4); *p += 4;
    uint16_t nv = htons(val); memcpy(*p, &nv, 2); *p += 2;
}

static void dmap_add_int(uint8_t **p, const char tag[4], uint32_t val)
{
    memcpy(*p, tag, 4); *p += 4;
    uint32_t len = htonl(4); memcpy(*p, &len, 4); *p += 4;
    uint32_t nv = htonl(val); memcpy(*p, &nv, 4); *p += 4;
}

static int dmap_add_string(uint8_t **p, const uint8_t *end, const char tag[4], const char *str)
{
    if (!str) str = "";
    size_t slen = strlen(str);
    if (*p + 8 + slen > end) return -1;
    memcpy(*p, tag, 4); *p += 4;
    uint32_t len = htonl((uint32_t)slen); memcpy(*p, &len, 4); *p += 4;
    memcpy(*p, str, slen); *p += slen;
    return 0;
}

static size_t dmap_encode_metadata(uint8_t *buf, size_t buf_size, const raop_metadata_t *meta)
{
    uint8_t inner[2048];
    uint8_t *p = inner;
    const uint8_t *end = inner + sizeof(inner);

    dmap_add_byte(&p, "mikd", 2);
    if (dmap_add_string(&p, end, "minm", meta->title) < 0) return 0;
    if (dmap_add_string(&p, end, "asar", meta->artist) < 0) return 0;
    if (dmap_add_string(&p, end, "asal", meta->album) < 0) return 0;
    if (dmap_add_string(&p, end, "asgn", meta->genre) < 0) return 0;
    dmap_add_int(&p, "astm", meta->duration_ms);
    dmap_add_short(&p, "astn", (uint16_t)meta->track_number);
    dmap_add_short(&p, "asdn", (uint16_t)meta->disc_number);

    size_t inner_len = (size_t)(p - inner);
    if (inner_len + 8 > buf_size) return 0;

    uint8_t *out = buf;
    memcpy(out, "mlit", 4); out += 4;
    uint32_t nlen = htonl((uint32_t)inner_len);
    memcpy(out, &nlen, 4); out += 4;
    memcpy(out, inner, inner_len); out += inner_len;

    return (size_t)(out - buf);
}

static int do_set_metadata(int fd, int *cseq, const char *session_url,
                           const char *session, uint32_t rtptime, const raop_metadata_t *meta)
{
    uint8_t dmap_buf[4096];
    size_t dmap_len = dmap_encode_metadata(dmap_buf, sizeof(dmap_buf), meta);
    if (dmap_len == 0) return -1;

    char hdr[1024];
    snprintf(hdr, sizeof(hdr),
        "SET_PARAMETER %s RTSP/1.0\r\nCSeq: %d\r\n"
        "Session: %s\r\n"
        "Content-Type: application/x-dmap-tagged\r\n"
        "RTP-Info: rtptime=%u\r\n"
        "Content-Length: %zu\r\n"
        COMMON_HEADERS "\r\n",
        session_url, (*cseq)++, session, rtptime, dmap_len);

    if (rtsp_send(fd, hdr, dmap_buf, dmap_len) < 0) return -1;

    char resp[2048];
    int status = 0;
    if (rtsp_recv(fd, resp, sizeof(resp), &status, NULL, NULL) < 0) return -1;
    RAOP_LOG("SET_PARAMETER(metadata) => %d", status);
    return (status == 200) ? 0 : -1;
}

static int do_set_artwork(int fd, int *cseq, const char *session_url,
                          const char *session, uint32_t rtptime,
                          const uint8_t *data, size_t len, int is_png)
{
    char hdr[1024];
    snprintf(hdr, sizeof(hdr),
        "SET_PARAMETER %s RTSP/1.0\r\nCSeq: %d\r\n"
        "Session: %s\r\n"
        "Content-Type: %s\r\n"
        "RTP-Info: rtptime=%u\r\n"
        "Content-Length: %zu\r\n"
        COMMON_HEADERS "\r\n",
        session_url, (*cseq)++, session,
        is_png ? "image/png" : "image/jpeg",
        rtptime, len);

    if (rtsp_send(fd, hdr, data, len) < 0) return -1;

    char resp[2048];
    int status = 0;
    if (rtsp_recv(fd, resp, sizeof(resp), &status, NULL, NULL) < 0) return -1;
    RAOP_LOG("SET_PARAMETER(artwork) => %d", status);
    return (status == 200) ? 0 : -1;
}

static int do_teardown(int fd, int *cseq, const char *session_url, const char *session)
{
    char hdr[512];
    snprintf(hdr, sizeof(hdr),
        "TEARDOWN %s RTSP/1.0\r\nCSeq: %d\r\n"
        "Session: %s\r\n"
        COMMON_HEADERS "\r\n",
        session_url, (*cseq)++, session);

    if (rtsp_send(fd, hdr, NULL, 0) < 0) return -1;

    char resp[2048];
    int status = 0;
    rtsp_recv(fd, resp, sizeof(resp), &status, NULL, NULL);
    RAOP_LOG("TEARDOWN => %d", status);
    return 0;
}

/* NTP epoch: 1900-01-01 to 1970-01-01 = 2208988800 seconds */
#define NTP_EPOCH_DELTA  0x83AA7E80U
#define NTP_FRAC_SCALE   4294967296.0

static void timespec_to_ntp(struct timespec *ts, uint32_t *sec, uint32_t *frac)
{
    *sec = (uint32_t)(ts->tv_sec + NTP_EPOCH_DELTA);
    *frac = (uint32_t)((double)ts->tv_nsec * 1e-9 * NTP_FRAC_SCALE);
}

static void *timing_thread_func(void *arg)
{
    raop_session_t *s = (raop_session_t *)arg;
    uint8_t req[32], res[32];
    struct sockaddr_in peer_addr;
    socklen_t peer_len;

    (void)0;

    while (s->timing_running) {
        peer_len = sizeof(peer_addr);
        ssize_t n = recvfrom(s->timing_fd, req, sizeof(req), 0,
                             (struct sockaddr *)&peer_addr, &peer_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            break;
        }
        if (n != 32 || req[0] != 0x80 || req[1] != 0xd2) continue;

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint32_t recv_sec, recv_frac;
        timespec_to_ntp(&now, &recv_sec, &recv_frac);

        memset(res, 0, sizeof(res));
        res[0] = 0x80;
        res[1] = 0xd3;
        res[2] = req[2];

        memcpy(res + 8, req + 24, 8);

        uint32_t ns = htonl(recv_sec);
        uint32_t nf = htonl(recv_frac);
        memcpy(res + 16, &ns, 4);
        memcpy(res + 20, &nf, 4);

        clock_gettime(CLOCK_MONOTONIC, &now);
        uint32_t xmit_sec, xmit_frac;
        timespec_to_ntp(&now, &xmit_sec, &xmit_frac);
        ns = htonl(xmit_sec);
        nf = htonl(xmit_frac);
        memcpy(res + 24, &ns, 4);
        memcpy(res + 28, &nf, 4);

        sendto(s->timing_fd, res, 32, 0,
               (struct sockaddr *)&peer_addr, peer_len);
    }

    (void)0;
    return NULL;
}

static int start_timing_listener(raop_session_t *s)
{
    s->timing_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s->timing_fd < 0) return -1;

    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY };
    if (bind(s->timing_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s->timing_fd); s->timing_fd = -1; return -1;
    }

    struct sockaddr_in bound;
    socklen_t blen = sizeof(bound);
    getsockname(s->timing_fd, (struct sockaddr *)&bound, &blen);
    s->timing_port = ntohs(bound.sin_port);

    struct timeval tv = { .tv_sec = 1 };
    setsockopt(s->timing_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    s->timing_running = 1;
    if (pthread_create(&s->timing_thread, NULL, timing_thread_func, s) != 0) {
        close(s->timing_fd); s->timing_fd = -1; return -1;
    }

    return 0;
}

static void stop_timing_listener(raop_session_t *s)
{
    if (!s->timing_running) return;
    s->timing_running = 0;
    pthread_join(s->timing_thread, NULL);
    if (s->timing_fd >= 0) { close(s->timing_fd); s->timing_fd = -1; }
}



static void send_sync_packet(raop_session_t *s, uint32_t cur_pos, uint32_t next_rtptime, int is_first)
{
    if (!s->rtp) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint32_t ntp_sec, ntp_frac;
    timespec_to_ntp(&now, &ntp_sec, &ntp_frac);

    uint8_t pkt[20];
    pkt[0] = is_first ? 0x90 : 0x80;
    pkt[1] = 0xd4;
    pkt[2] = 0x00;
    pkt[3] = 0x07;

    uint32_t cp = htonl(cur_pos);
    memcpy(pkt + 4, &cp, 4);

    uint32_t ns = htonl(ntp_sec);
    uint32_t nf = htonl(ntp_frac);
    memcpy(pkt + 8, &ns, 4);
    memcpy(pkt + 12, &nf, 4);

    uint32_t rt = htonl(next_rtptime);
    memcpy(pkt + 16, &rt, 4);

    rtp_stream_send_control(s->rtp, pkt, 20);
}

static void ffmpeg_log_callback(void *ptr, int level, const char *fmt, va_list vl)
{
    if (level > AV_LOG_VERBOSE) return;
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, vl);
    buf[strcspn(buf, "\n")] = 0;
    if (buf[0]) RAOP_LOG("ffmpeg: %s", buf);
}

static struct audio_decoder *audio_decoder_open(const char *path)
{
    struct audio_decoder *dec = calloc(1, sizeof(*dec));
    if (!dec) return NULL;

    RAOP_LOG("audio_decoder_open: %s", path ? path : "(null)");
    static int ff_net_inited = 0;
    if (!ff_net_inited) { avformat_network_init(); ff_net_inited = 1; }
    av_log_set_level(AV_LOG_VERBOSE);
    av_log_set_callback(ffmpeg_log_callback);

    AVDictionary *opts = NULL;
    av_dict_set(&opts, "tls_verify", "0", 0);
    av_dict_set(&opts, "user_agent", "Tempo/1.0", 0);
    av_dict_set(&opts, "timeout", "15000000", 0);
    av_dict_set(&opts, "reconnect", "1", 0);
    int ret = avformat_open_input(&dec->fmt_ctx, path, NULL, &opts);
    if (opts) {
        AVDictionaryEntry *e = NULL;
        while ((e = av_dict_get(opts, "", e, AV_DICT_IGNORE_SUFFIX)))
            RAOP_LOG("unused opt: %s=%s", e->key, e->value);
    }
    av_dict_free(&opts);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        RAOP_LOG("avformat_open_input failed: %s (ret=%d)", errbuf, ret);
        goto fail;
    }
    RAOP_LOG("avformat_open_input OK");
    if (avformat_find_stream_info(dec->fmt_ctx, NULL) < 0)
        goto fail;

    dec->stream_idx = av_find_best_stream(dec->fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (dec->stream_idx < 0)
        goto fail;

    AVStream *stream = dec->fmt_ctx->streams[dec->stream_idx];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
        goto fail;

    dec->dec_ctx = avcodec_alloc_context3(codec);
    if (!dec->dec_ctx)
        goto fail;
    if (avcodec_parameters_to_context(dec->dec_ctx, stream->codecpar) < 0)
        goto fail;
    if (avcodec_open2(dec->dec_ctx, codec, NULL) < 0)
        goto fail;

    if (swr_alloc_set_opts2(&dec->swr,
            &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, 44100,
            &dec->dec_ctx->ch_layout, dec->dec_ctx->sample_fmt, dec->dec_ctx->sample_rate,
            0, NULL) < 0)
        goto fail;
    if (swr_init(dec->swr) < 0)
        goto fail;

    AVDictionaryEntry *tag;
    tag = av_dict_get(dec->fmt_ctx->metadata, "title", NULL, 0);
    if (tag) strncpy(dec->title, tag->value, sizeof(dec->title) - 1);
    tag = av_dict_get(dec->fmt_ctx->metadata, "artist", NULL, 0);
    if (tag) strncpy(dec->artist, tag->value, sizeof(dec->artist) - 1);
    tag = av_dict_get(dec->fmt_ctx->metadata, "album", NULL, 0);
    if (tag) strncpy(dec->album, tag->value, sizeof(dec->album) - 1);
    tag = av_dict_get(dec->fmt_ctx->metadata, "genre", NULL, 0);
    if (tag) strncpy(dec->genre, tag->value, sizeof(dec->genre) - 1);

    if (!dec->title[0]) {
        const char *base = strrchr(path, '/');
        strncpy(dec->title, base ? base + 1 : path, sizeof(dec->title) - 1);
    }

    dec->duration_ms = (uint32_t)(dec->fmt_ctx->duration / (AV_TIME_BASE / 1000));
    dec->total_samples = (uint32_t)((double)dec->fmt_ctx->duration / AV_TIME_BASE * 44100);

    dec->frame = av_frame_alloc();
    dec->pkt = av_packet_alloc();
    if (!dec->frame || !dec->pkt)
        goto fail;

    return dec;

fail:
    if (dec->swr) swr_free(&dec->swr);
    if (dec->frame) av_frame_free(&dec->frame);
    if (dec->pkt) av_packet_free(&dec->pkt);
    if (dec->dec_ctx) avcodec_free_context(&dec->dec_ctx);
    if (dec->fmt_ctx) avformat_close_input(&dec->fmt_ctx);
    free(dec);
    return NULL;
}

static int audio_decoder_read(struct audio_decoder *dec, int16_t *pcm, int nsamples)
{
    int written = 0;

    while (dec->buf_samples > 0 && written < nsamples) {
        int avail = dec->buf_samples;
        int need = nsamples - written;
        int copy = avail < need ? avail : need;
        memcpy(pcm + written * CHANNELS, dec->resample_buf + dec->buf_offset * CHANNELS,
               copy * CHANNELS * sizeof(int16_t));
        written += copy;
        dec->buf_offset += copy;
        dec->buf_samples -= copy;
    }

    while (written < nsamples) {
        int ret = av_read_frame(dec->fmt_ctx, dec->pkt);
        if (ret < 0) break;

        if (dec->pkt->stream_index != dec->stream_idx) {
            av_packet_unref(dec->pkt);
            continue;
        }

        ret = avcodec_send_packet(dec->dec_ctx, dec->pkt);
        av_packet_unref(dec->pkt);
        if (ret < 0) continue;

        while (written < nsamples) {
            ret = avcodec_receive_frame(dec->dec_ctx, dec->frame);
            if (ret < 0) break;

            int max_out = dec->frame->nb_samples * 44100 / dec->dec_ctx->sample_rate + 256;
            if (max_out > 8192)
                max_out = 8192;

            uint8_t *out_buf = (uint8_t *)dec->resample_buf;
            int converted = swr_convert(dec->swr, &out_buf, max_out,
                                        (const uint8_t **)dec->frame->extended_data,
                                        dec->frame->nb_samples);
            if (converted <= 0) continue;

            dec->buf_offset = 0;
            dec->buf_samples = converted;

            int need = nsamples - written;
            int copy = dec->buf_samples < need ? dec->buf_samples : need;
            memcpy(pcm + written * CHANNELS, dec->resample_buf,
                   copy * CHANNELS * sizeof(int16_t));
            written += copy;
            dec->buf_offset += copy;
            dec->buf_samples -= copy;
        }
    }

    return written;
}

static int audio_decoder_seek(struct audio_decoder *dec, uint32_t position_ms)
{
    int64_t ts = (int64_t)position_ms * AV_TIME_BASE / 1000;
    int ret = av_seek_frame(dec->fmt_ctx, -1, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(dec->dec_ctx);
    dec->buf_samples = 0;
    dec->buf_offset = 0;
    return ret < 0 ? -1 : 0;
}

static void audio_decoder_close(struct audio_decoder *dec)
{
    if (!dec) return;
    swr_free(&dec->swr);
    av_frame_free(&dec->frame);
    av_packet_free(&dec->pkt);
    avcodec_free_context(&dec->dec_ctx);
    avformat_close_input(&dec->fmt_ctx);
    free(dec);
}

static void process_cmd(raop_session_t *s, struct raop_cmd *cmd,
                        struct audio_decoder **adec_ptr, uint64_t *start_ns, int *pkt)
{
    uint32_t cur_ts = rtp_stream_get_timestamp(s->rtp);
    uint16_t cur_seq = rtp_stream_get_seq(s->rtp);

    switch (cmd->type) {
    case RAOP_CMD_VOLUME:
        s->current_volume_db = cmd->volume_db;
        do_set_volume(s->sockfd, &s->cseq, s->session_url, s->rtsp_session, cmd->volume_db);
        break;

    case RAOP_CMD_METADATA:
        do_set_metadata(s->sockfd, &s->cseq, s->session_url, s->rtsp_session,
                        cur_ts, &cmd->metadata);
        raop_cmd_free_metadata(cmd);
        break;

    case RAOP_CMD_ARTWORK:
        do_set_artwork(s->sockfd, &s->cseq, s->session_url, s->rtsp_session,
                       cur_ts, cmd->artwork_data, cmd->artwork_len, cmd->artwork_is_png);
        free(cmd->artwork_data);
        cmd->artwork_data = NULL;
        break;

    case RAOP_CMD_PROGRESS:
        do_set_progress(s->sockfd, &s->cseq, s->session_url, s->rtsp_session,
                        cmd->progress_start, cmd->progress_cur, cmd->progress_end);
        break;

    case RAOP_CMD_PAUSE: {
        s->paused = 1;
        s->state = RAOP_STATE_PAUSED;
        if (s->state_cb) s->state_cb(s, RAOP_STATE_PAUSED, s->user_data);
        int keepalive_counter = 0;

        while (s->paused && s->running) {
            struct timespec pause_sleep = { 0, 100000000 };
            nanosleep(&pause_sleep, NULL);
            keepalive_counter++;

            if (keepalive_counter >= 200) {
                uint32_t ts = rtp_stream_get_timestamp(s->rtp);
                uint32_t end_rt = s->start_rtptime + s->total_samples;
                do_set_progress(s->sockfd, &s->cseq, s->session_url, s->rtsp_session,
                                s->start_rtptime, ts, end_rt);
                keepalive_counter = 0;
            }

            pthread_mutex_lock(&s->cmd_mutex);
            struct raop_cmd inner = s->pending_cmd;
            s->pending_cmd.type = RAOP_CMD_NONE;
            s->pending_cmd.play_path = NULL;
            s->pending_cmd.artwork_data = NULL;
            memset(&s->pending_cmd.metadata, 0, sizeof(s->pending_cmd.metadata));
            pthread_mutex_unlock(&s->cmd_mutex);

            if (inner.type == RAOP_CMD_RESUME) {
                s->paused = 0;

                struct timespec now_ts;
                clock_gettime(CLOCK_MONOTONIC, &now_ts);
                *start_ns = (uint64_t)now_ts.tv_sec * 1000000000ULL + now_ts.tv_nsec;
                *pkt = 0;

                uint32_t ts = rtp_stream_get_timestamp(s->rtp);
                send_sync_packet(s, ts, ts, 1);

                s->state = RAOP_STATE_STREAMING;
                if (s->state_cb) s->state_cb(s, RAOP_STATE_STREAMING, s->user_data);
            } else if (inner.type == RAOP_CMD_VOLUME) {
                s->current_volume_db = inner.volume_db;
                do_set_volume(s->sockfd, &s->cseq, s->session_url, s->rtsp_session, inner.volume_db);
            } else if (inner.type != RAOP_CMD_NONE) {
                process_cmd(s, &inner, adec_ptr, start_ns, pkt);
            }
        }
        break;
    }

    case RAOP_CMD_SEEK:
        if (s->audio_file && *adec_ptr) {
            audio_decoder_seek(*adec_ptr, cmd->seek_position_ms);

            uint32_t seek_samples = (uint32_t)((uint64_t)cmd->seek_position_ms * SAMPLE_RATE / 1000);
            uint32_t new_ts = s->start_rtptime + seek_samples;
            rtp_stream_set_position(s->rtp, rtp_stream_get_seq(s->rtp), new_ts);

            uint32_t end_rtptime = s->start_rtptime + s->total_samples;
            do_set_progress(s->sockfd, &s->cseq, s->session_url, s->rtsp_session,
                            s->start_rtptime, new_ts, end_rtptime);

            send_sync_packet(s, new_ts, new_ts, 1);

            struct timespec now_ts;
            clock_gettime(CLOCK_MONOTONIC, &now_ts);
            *start_ns = (uint64_t)now_ts.tv_sec * 1000000000ULL + now_ts.tv_nsec;
            *pkt = 0;
        }
        break;

    case RAOP_CMD_PLAY:
        RAOP_LOG("process_cmd PLAY: push_mode=%d, path=%s",
                 s->push_mode, cmd->play_path ? cmd->play_path : "(null)");
        if (*adec_ptr) { audio_decoder_close(*adec_ptr); *adec_ptr = NULL; }
        free(s->audio_file);
        s->audio_file = cmd->play_path ? strdup(cmd->play_path) : NULL;
        free(cmd->play_path);
        cmd->play_path = NULL;
        if (s->audio_file) {
            *adec_ptr = audio_decoder_open(s->audio_file);
            if (*adec_ptr) {
                s->total_samples = (*adec_ptr)->total_samples;
                RAOP_LOG("Track switch: %s (%u samples)", s->audio_file, s->total_samples);
                if ((*adec_ptr)->title[0]) {
                    raop_metadata_t meta = {0};
                    meta.title = (*adec_ptr)->title;
                    meta.artist = (*adec_ptr)->artist[0] ? (*adec_ptr)->artist : NULL;
                    meta.album = (*adec_ptr)->album[0] ? (*adec_ptr)->album : NULL;
                    meta.genre = (*adec_ptr)->genre[0] ? (*adec_ptr)->genre : NULL;
                    meta.duration_ms = (*adec_ptr)->duration_ms;
                    do_set_metadata(s->sockfd, &s->cseq, s->session_url, s->rtsp_session,
                                    rtp_stream_get_timestamp(s->rtp), &meta);
                }
            }
        }
        if (s->push_mode)
            s->total_samples = SAMPLE_RATE * 3600 * 3;
        else if (!*adec_ptr)
            s->total_samples = 500 * SAMPLES_PER_PACKET;
        s->start_rtptime = rtp_stream_get_timestamp(s->rtp);
        if (cmd->seek_position_ms > 0 && *adec_ptr) {
            audio_decoder_seek(*adec_ptr, cmd->seek_position_ms);
            uint32_t seek_samples = (uint32_t)((uint64_t)cmd->seek_position_ms * SAMPLE_RATE / 1000);
            uint32_t new_ts = s->start_rtptime + seek_samples;
            rtp_stream_set_position(s->rtp, rtp_stream_get_seq(s->rtp), new_ts);
            RAOP_LOG("Track switch seek to %u ms", cmd->seek_position_ms);
        }
        {
            uint32_t cur_ts_play = rtp_stream_get_timestamp(s->rtp);
            uint32_t end_rt = s->start_rtptime + s->total_samples;
            do_set_progress(s->sockfd, &s->cseq, s->session_url, s->rtsp_session,
                            s->start_rtptime, cur_ts_play, end_rt);
            send_sync_packet(s, cur_ts_play, cur_ts_play, 1);
        }
        if (s->push_mode) {
            if (s->alac_enc)
                avcodec_flush_buffers(s->alac_enc->ctx);
            pthread_mutex_lock(&s->rb_mutex);
            int wait_loops = 0;
            while (s->running && s->push_mode && wait_loops < 50) {
                int avail = (s->rb_write - s->rb_read + s->rb_capacity) % s->rb_capacity;
                if (avail >= SAMPLES_PER_PACKET * 4) break;
                struct timespec abstime;
                clock_gettime(CLOCK_REALTIME, &abstime);
                abstime.tv_nsec += 20000000;
                if (abstime.tv_nsec >= 1000000000) {
                    abstime.tv_sec++;
                    abstime.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&s->rb_cond, &s->rb_mutex, &abstime);
                wait_loops++;
            }
            pthread_mutex_unlock(&s->rb_mutex);
            RAOP_LOG("Track switch: waited %d loops for buffer fill", wait_loops);
        }
        {
            struct timespec now_ts;
            clock_gettime(CLOCK_MONOTONIC, &now_ts);
            *start_ns = (uint64_t)now_ts.tv_sec * 1000000000ULL + now_ts.tv_nsec;
            *pkt = 0;
        }
        break;

    default:
        break;
    }
}

enum raop_phase {
    RAOP_PHASE_IDLE,
    RAOP_PHASE_STREAMING,
    RAOP_PHASE_TEARDOWN,
    RAOP_PHASE_ERROR,
};

static int raop_rtsp_handshake(raop_session_t *s)
{
    char local_ip[INET_ADDRSTRLEN];
    struct sockaddr_in local_sa;
    socklen_t local_sa_len = sizeof(local_sa);
    getsockname(s->sockfd, (struct sockaddr *)&local_sa, &local_sa_len);
    inet_ntop(AF_INET, &local_sa.sin_addr, local_ip, sizeof(local_ip));

    s->session_id = (uint32_t)rand();
    snprintf(s->session_url, sizeof(s->session_url), "rtsp://%s/%u", local_ip, s->session_id);

    for (int retry = 0; do_options(s->sockfd, &s->cseq) < 0; retry++) {
        if (retry >= 3 || !s->running) return -1;
        RAOP_LOG("OPTIONS failed (attempt %d), resetting connection...", retry + 1);
        char teardown_hdr[512];
        snprintf(teardown_hdr, sizeof(teardown_hdr),
            "TEARDOWN rtsp://localhost/1 RTSP/1.0\r\nCSeq: 1\r\n"
            "Session: 1\r\n" COMMON_HEADERS "\r\n");
        send_raw(s->sockfd, teardown_hdr, strlen(teardown_hdr));
        close(s->sockfd);
        s->sockfd = -1;

        struct timespec retry_wait = { 3, 0 };
        nanosleep(&retry_wait, NULL);
        if (!s->running) return -1;

        s->sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (s->sockfd < 0) return -1;
        enable_tcp_keepalive(s->sockfd);
        struct sockaddr_in reconn = { .sin_family = AF_INET, .sin_port = htons(s->port) };
        inet_pton(AF_INET, s->host, &reconn.sin_addr);
        if (connect(s->sockfd, (struct sockaddr *)&reconn, sizeof(reconn)) < 0) return -1;

        getsockname(s->sockfd, (struct sockaddr *)&local_sa, &local_sa_len);
        inet_ntop(AF_INET, &local_sa.sin_addr, local_ip, sizeof(local_ip));
        s->session_id = (uint32_t)rand();
        snprintf(s->session_url, sizeof(s->session_url), "rtsp://%s/%u", local_ip, s->session_id);
        s->cseq = 1;
    }

    do_auth_setup(s->sockfd, &s->cseq);

    if (do_announce(s->sockfd, &s->cseq, s->session_url, local_ip, s->host, s->session_id) < 0)
        return -1;

    uint16_t la_port = 0, lc_port = 0, lt_port = 0;
    s->rtp = rtp_stream_create(s->host, &la_port, &lc_port, &lt_port);
    if (!s->rtp) return -1;

    if (start_timing_listener(s) < 0) return -1;

    uint16_t server_port = 0, remote_ctrl_port = 0, remote_timing_port = 0;
    if (do_setup(s->sockfd, &s->cseq, s->session_url,
                 lc_port, s->timing_port,
                 &server_port, &remote_ctrl_port, &remote_timing_port,
                 s->rtsp_session, sizeof(s->rtsp_session)) < 0)
        return -1;

    if (server_port == 0) return -1;

    if (remote_ctrl_port == 0) remote_ctrl_port = lc_port;
    if (remote_timing_port == 0) remote_timing_port = lt_port;
    rtp_stream_set_server_ports(s->rtp, server_port, remote_ctrl_port, remote_timing_port);

    uint16_t initial_seq = rtp_stream_get_seq(s->rtp);
    s->base_rtptime = rtp_stream_get_timestamp(s->rtp);
    if (do_record(s->sockfd, &s->cseq, s->session_url, s->rtsp_session,
                  initial_seq, s->base_rtptime) < 0)
        return -1;

    double remote_vol = 0.0;
    if (do_get_volume(s->sockfd, &s->cseq, s->session_url, s->rtsp_session, &remote_vol) == 0) {
        s->current_volume_db = remote_vol;
        RAOP_LOG("Got remote volume: %.1f dB", remote_vol);
    } else {
        s->current_volume_db = 0.0;
        do_set_volume(s->sockfd, &s->cseq, s->session_url, s->rtsp_session, s->current_volume_db);
    }

    return 0;
}

static enum raop_phase raop_idle_loop(raop_session_t *s)
{
    int idle_counter = 0;
    const int IDLE_TIMEOUT_TICKS = 6000;

    while (s->running) {
        struct timespec idle_sleep = { 0, 100000000 };
        nanosleep(&idle_sleep, NULL);
        idle_counter++;

        if (idle_counter % 200 == 0) {
            uint32_t ts = rtp_stream_get_timestamp(s->rtp);
            do_set_progress(s->sockfd, &s->cseq, s->session_url, s->rtsp_session,
                            ts, ts, ts + SAMPLES_PER_PACKET * 100);
        }

        if (idle_counter >= IDLE_TIMEOUT_TICKS) {
            RAOP_LOG("Idle timeout (10min), disconnecting");
            return RAOP_PHASE_TEARDOWN;
        }

        pthread_mutex_lock(&s->cmd_mutex);
        struct raop_cmd cmd = s->pending_cmd;
        s->pending_cmd.type = RAOP_CMD_NONE;
        s->pending_cmd.play_path = NULL;
        s->pending_cmd.artwork_data = NULL;
        memset(&s->pending_cmd.metadata, 0, sizeof(s->pending_cmd.metadata));
        pthread_mutex_unlock(&s->cmd_mutex);

        if (cmd.type == RAOP_CMD_PLAY) {
            RAOP_LOG("idle_loop: PLAY cmd received, push_mode=%d, path=%s",
                     s->push_mode, cmd.play_path ? cmd.play_path : "(null)");
            free(s->audio_file);
            s->audio_file = cmd.play_path;
            cmd.play_path = NULL;
            s->pending_cmd.seek_position_ms = cmd.seek_position_ms;
            return RAOP_PHASE_STREAMING;
        } else if (cmd.type == RAOP_CMD_VOLUME) {
            s->current_volume_db = cmd.volume_db;
            do_set_volume(s->sockfd, &s->cseq, s->session_url, s->rtsp_session, cmd.volume_db);
        }
    }
    return RAOP_PHASE_TEARDOWN;
}

static enum raop_phase raop_streaming_loop(raop_session_t *s)
{
    RAOP_LOG("entering streaming: push_mode=%d, audio_file=%s",
             s->push_mode, s->audio_file ? s->audio_file : "(null)");
    s->state = RAOP_STATE_STREAMING;
    if (s->state_cb) s->state_cb(s, RAOP_STATE_STREAMING, s->user_data);

    int16_t pcm[SAMPLES_PER_PACKET * CHANNELS];
    uint8_t alac_buf[2048];

    s->alac_enc = alac_encoder_create();
    if (!s->alac_enc) {
        RAOP_LOGE("Failed to create ALAC encoder");
        return RAOP_PHASE_ERROR;
    }
    RAOP_LOG("ALAC compressed encoder ready");

    struct audio_decoder *adec = NULL;
    char file_title[256] = {0}, file_artist[256] = {0};
    char file_album[256] = {0}, file_genre[256] = {0};
    uint32_t file_duration_ms = 0;

    if (s->audio_file) {
        adec = audio_decoder_open(s->audio_file);
        if (adec) {
            s->total_samples = adec->total_samples;
            file_duration_ms = adec->duration_ms;
            strncpy(file_title, adec->title, sizeof(file_title) - 1);
            strncpy(file_artist, adec->artist, sizeof(file_artist) - 1);
            strncpy(file_album, adec->album, sizeof(file_album) - 1);
            strncpy(file_genre, adec->genre, sizeof(file_genre) - 1);
        }
    }
    if (s->push_mode)
        s->total_samples = SAMPLE_RATE * 3600 * 3;
    else if (s->total_samples == 0)
        s->total_samples = 500 * SAMPLES_PER_PACKET;

    s->start_rtptime = s->base_rtptime;

    uint32_t initial_seek_ms = s->pending_cmd.seek_position_ms;
    s->pending_cmd.seek_position_ms = 0;
    if (initial_seek_ms > 0 && adec) {
        audio_decoder_seek(adec, initial_seek_ms);
        uint32_t seek_samples = (uint32_t)((uint64_t)initial_seek_ms * SAMPLE_RATE / 1000);
        uint32_t new_ts = s->start_rtptime + seek_samples;
        rtp_stream_set_position(s->rtp, rtp_stream_get_seq(s->rtp), new_ts);
        RAOP_LOG("Initial seek to %u ms", initial_seek_ms);
    }

    if (s->push_mode) {
        pthread_mutex_lock(&s->rb_mutex);
        int wait_loops = 0;
        while (s->running && s->push_mode && wait_loops < 50) {
            int avail = (s->rb_write - s->rb_read + s->rb_capacity) % s->rb_capacity;
            if (avail >= SAMPLES_PER_PACKET * 4) break;
            struct timespec abstime;
            clock_gettime(CLOCK_REALTIME, &abstime);
            abstime.tv_nsec += 20000000;
            if (abstime.tv_nsec >= 1000000000) {
                abstime.tv_sec++;
                abstime.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&s->rb_cond, &s->rb_mutex, &abstime);
            wait_loops++;
        }
        pthread_mutex_unlock(&s->rb_mutex);
        RAOP_LOG("Initial streaming: waited %d loops for buffer fill", wait_loops);
    }

    uint32_t cur_pos_ts = rtp_stream_get_timestamp(s->rtp);
    uint32_t end_rtptime = s->start_rtptime + s->total_samples;
    do_set_progress(s->sockfd, &s->cseq, s->session_url, s->rtsp_session,
                    s->start_rtptime, cur_pos_ts, end_rtptime);

    if (s->audio_file && file_title[0]) {
        raop_metadata_t filemeta = {0};
        filemeta.title = file_title;
        filemeta.artist = file_artist[0] ? file_artist : NULL;
        filemeta.album = file_album[0] ? file_album : NULL;
        filemeta.genre = file_genre[0] ? file_genre : NULL;
        filemeta.duration_ms = file_duration_ms;
        do_set_metadata(s->sockfd, &s->cseq, s->session_url, s->rtsp_session,
                        rtp_stream_get_timestamp(s->rtp), &filemeta);
        RAOP_LOG("Metadata: %s - %s [%s]",
                 file_artist[0] ? file_artist : "?",
                 file_title,
                 file_album[0] ? file_album : "?");
    }

    uint32_t cur_rtptime = rtp_stream_get_timestamp(s->rtp);
    send_sync_packet(s, cur_rtptime, cur_rtptime, 1);

    int pkt = 0;
    struct timespec start_ts;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);
    uint64_t start_ns = (uint64_t)start_ts.tv_sec * 1000000000ULL + start_ts.tv_nsec;
    uint64_t ns_per_pkt = (uint64_t)SAMPLES_PER_PACKET * 1000000000ULL / SAMPLE_RATE;

    while (s->running) {
        pthread_mutex_lock(&s->cmd_mutex);
        struct raop_cmd cmd = s->pending_cmd;
        s->pending_cmd.type = RAOP_CMD_NONE;
        s->pending_cmd.play_path = NULL;
        s->pending_cmd.artwork_data = NULL;
        memset(&s->pending_cmd.metadata, 0, sizeof(s->pending_cmd.metadata));
        pthread_mutex_unlock(&s->cmd_mutex);

        if (cmd.type != RAOP_CMD_NONE)
            process_cmd(s, &cmd, &adec, &start_ns, &pkt);

        if (!s->running) break;

        if (s->push_mode) {
            pthread_mutex_lock(&s->rb_mutex);
            int avail = (s->rb_write - s->rb_read + s->rb_capacity) % s->rb_capacity;
            if (avail < SAMPLES_PER_PACKET) {
                struct timespec abstime;
                clock_gettime(CLOCK_REALTIME, &abstime);
                abstime.tv_nsec += 20000000;
                if (abstime.tv_nsec >= 1000000000) {
                    abstime.tv_sec++;
                    abstime.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&s->rb_cond, &s->rb_mutex, &abstime);
                avail = (s->rb_write - s->rb_read + s->rb_capacity) % s->rb_capacity;
            }
            int got = avail < SAMPLES_PER_PACKET ? avail : SAMPLES_PER_PACKET;
            for (int i = 0; i < got; i++) {
                pcm[i * CHANNELS]     = s->pcm_ring[s->rb_read * CHANNELS];
                pcm[i * CHANNELS + 1] = s->pcm_ring[s->rb_read * CHANNELS + 1];
                s->rb_read = (s->rb_read + 1) % s->rb_capacity;
            }
            pthread_cond_signal(&s->rb_cond);
            pthread_mutex_unlock(&s->rb_mutex);
            if (got < SAMPLES_PER_PACKET) {
                memset(pcm + got * CHANNELS, 0,
                       (SAMPLES_PER_PACKET - got) * CHANNELS * sizeof(int16_t));
            }
            if (pkt % 125 == 0) {
                RAOP_LOG("push pkt=%d got=%d/%d rb=%d/%d",
                         pkt, got, SAMPLES_PER_PACKET, avail, s->rb_capacity);
            }
            if (got == 0) {
                RAOP_LOGW("push underrun pkt=%d rb_empty", pkt);
            }
        } else if (adec) {
            int got = audio_decoder_read(adec, pcm, SAMPLES_PER_PACKET);
            if (got <= 0) break;
            if (got < SAMPLES_PER_PACKET)
                memset(pcm + got * CHANNELS, 0, (SAMPLES_PER_PACKET - got) * CHANNELS * sizeof(int16_t));
        } else {
            memset(pcm, 0, SAMPLES_PER_PACKET * CHANNELS * sizeof(int16_t));
            if (pkt >= 500) break;
        }

        int alac_len = alac_encode_compressed(s->alac_enc, alac_buf, sizeof(alac_buf),
                                              pcm, SAMPLES_PER_PACKET);
        if (alac_len < 0) {
            RAOP_LOGE("ALAC encode failed pkt=%d", pkt);
            break;
        }
        int send_ret = rtp_stream_send_audio(s->rtp, alac_buf, alac_len);
        if (send_ret < 0 && pkt % 125 == 0) {
            RAOP_LOGW("rtp_send failed pkt=%d", pkt);
        }
        pkt++;

        if (pkt % 126 == 0) {
            uint32_t pos = rtp_stream_get_timestamp(s->rtp);
            send_sync_packet(s, pos - SAMPLES_PER_PACKET, pos, 0);
        }

        if (pkt % 625 == 0 && pkt > 0) {
            int sock_ok = check_rtsp_socket(s->sockfd);
            if (sock_ok < 0) {
                RAOP_LOGE("RTSP socket dead pkt=%d", pkt);
                s->running = 0;
                break;
            }
            uint32_t pos = rtp_stream_get_timestamp(s->rtp);
            uint32_t end_rt = s->start_rtptime + s->total_samples;
            int ka_ret = do_set_progress(s->sockfd, &s->cseq, s->session_url,
                                s->rtsp_session, s->start_rtptime, pos, end_rt);
            RAOP_LOG("keepalive pkt=%d sock=%d ka=%d", pkt, sock_ok, ka_ret);
            if (ka_ret < 0) {
                RAOP_LOGE("RTSP keepalive failed pkt=%d", pkt);
                s->running = 0;
                break;
            }
        }

        uint64_t target_ns = start_ns + (uint64_t)pkt * ns_per_pkt;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;
        if (target_ns > now_ns) {
            struct timespec rem = {
                .tv_sec = (target_ns - now_ns) / 1000000000ULL,
                .tv_nsec = (target_ns - now_ns) % 1000000000ULL
            };
            nanosleep(&rem, NULL);
        }
    }

    if (adec) { audio_decoder_close(adec); adec = NULL; }
    if (s->alac_enc) { alac_encoder_destroy(s->alac_enc); s->alac_enc = NULL; }
    RAOP_LOGI("streaming exit pkt=%d (%.1fs) running=%d",
             pkt, (double)pkt * SAMPLES_PER_PACKET / SAMPLE_RATE, s->running);

    if (s->running) {
        RAOP_LOGI("-> IDLE (streaming loop ended but still running)");
        s->state = RAOP_STATE_IDLE;
        if (s->state_cb) s->state_cb(s, RAOP_STATE_IDLE, s->user_data);
        return RAOP_PHASE_IDLE;
    }

    return RAOP_PHASE_TEARDOWN;
}

static void *raop_thread(void *arg)
{
    raop_session_t *s = (raop_session_t *)arg;
    s->cseq = 1;

    s->state = RAOP_STATE_CONNECTED;
    if (s->state_cb) s->state_cb(s, RAOP_STATE_CONNECTED, s->user_data);

    if (raop_rtsp_handshake(s) < 0)
        goto cleanup;

    enum raop_phase phase;
    if (!s->audio_file) {
        s->state = RAOP_STATE_IDLE;
        if (s->state_cb) s->state_cb(s, RAOP_STATE_IDLE, s->user_data);
        phase = RAOP_PHASE_IDLE;
    } else {
        phase = RAOP_PHASE_STREAMING;
    }

    while (s->running && phase != RAOP_PHASE_TEARDOWN && phase != RAOP_PHASE_ERROR) {
        if (phase == RAOP_PHASE_IDLE)
            phase = raop_idle_loop(s);
        else
            phase = raop_streaming_loop(s);
    }

    if (phase != RAOP_PHASE_ERROR)
        do_teardown(s->sockfd, &s->cseq, s->session_url, s->rtsp_session);

cleanup:
    if (s->alac_enc) { alac_encoder_destroy(s->alac_enc); s->alac_enc = NULL; }
    stop_timing_listener(s);
    if (s->rtp) { rtp_stream_destroy(s->rtp); s->rtp = NULL; }
    if (s->sockfd >= 0) { close(s->sockfd); s->sockfd = -1; }
    s->running = 0;
    s->state = RAOP_STATE_STOPPED;
    if (s->state_cb) s->state_cb(s, RAOP_STATE_STOPPED, s->user_data);
    return NULL;
}

raop_session_t *raop_session_new(const char *host, uint16_t port, raop_state_cb cb, void *ud)
{
    if (!host || !host[0] || !cb) return NULL;
    raop_session_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->host = strdup(host);
    s->port = port;
    s->state = RAOP_STATE_STOPPED;
    s->sockfd = -1;
    s->timing_fd = -1;
    s->current_volume_db = -15.0;
    pthread_mutex_init(&s->cmd_mutex, NULL);
    pthread_mutex_init(&s->rb_mutex, NULL);
    pthread_cond_init(&s->rb_cond, NULL);
    s->state_cb = cb;
    s->user_data = ud;

    static int seeded = 0;
    if (!seeded) { srand((unsigned)time(NULL)); seeded = 1; }
    return s;
}

void raop_session_free(raop_session_t *s)
{
    if (!s) return;
    raop_session_stop(s);
    pthread_mutex_destroy(&s->rb_mutex);
    pthread_cond_destroy(&s->rb_cond);
    free(s->pcm_ring);
    s->pcm_ring = NULL;
    raop_cmd_free_metadata(&s->pending_cmd);
    free(s->pending_cmd.artwork_data);
    free(s->pending_cmd.play_path);
    pthread_mutex_destroy(&s->cmd_mutex);
    free(s->host);
    free(s->audio_file);
    free(s);
}

int raop_session_connect(raop_session_t *s)
{
    if (!s || s->state != RAOP_STATE_STOPPED) return -1;

    free(s->audio_file);
    s->audio_file = NULL;

    RAOP_LOG("Connecting to %s:%d (connect-only)", s->host, s->port);
    s->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->sockfd < 0) return -1;
    enable_tcp_keepalive(s->sockfd);

    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(s->port) };
    inet_pton(AF_INET, s->host, &addr.sin_addr);
    if (connect(s->sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s->sockfd); s->sockfd = -1; return -1;
    }

    s->running = 1;
    if (pthread_create(&s->thread, NULL, raop_thread, s) != 0) {
        close(s->sockfd); s->sockfd = -1; return -1;
    }
    return 0;
}

int raop_session_play(raop_session_t *s, const char *path, uint32_t start_ms)
{
    if (!s) return -1;

    if (s->state == RAOP_STATE_IDLE || s->state == RAOP_STATE_PAUSED
            || s->state == RAOP_STATE_STREAMING) {
        pthread_mutex_lock(&s->cmd_mutex);
        s->pending_cmd.type = RAOP_CMD_PLAY;
        free(s->pending_cmd.play_path);
        s->pending_cmd.play_path = path ? strdup(path) : NULL;
        s->pending_cmd.seek_position_ms = start_ms;
        pthread_mutex_unlock(&s->cmd_mutex);
        return 0;
    }

    if (s->state != RAOP_STATE_STOPPED) return -1;

    free(s->audio_file);
    s->audio_file = path ? strdup(path) : NULL;
    s->pending_cmd.seek_position_ms = start_ms;

    RAOP_LOG("Connecting to %s:%d", s->host, s->port);
    s->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->sockfd < 0) return -1;
    enable_tcp_keepalive(s->sockfd);

    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(s->port) };
    inet_pton(AF_INET, s->host, &addr.sin_addr);
    if (connect(s->sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s->sockfd); s->sockfd = -1; return -1;
    }

    s->running = 1;
    if (pthread_create(&s->thread, NULL, raop_thread, s) != 0) {
        close(s->sockfd); s->sockfd = -1; return -1;
    }
    return 0;
}

int raop_session_stop(raop_session_t *s)
{
    if (!s || !s->running) return 0;
    s->running = 0;
    s->paused = 0;
    pthread_join(s->thread, NULL);
    return 0;
}

int raop_session_pause(raop_session_t *s)
{
    if (!s || s->state != RAOP_STATE_STREAMING) return -1;
    pthread_mutex_lock(&s->cmd_mutex);
    s->pending_cmd.type = RAOP_CMD_PAUSE;
    pthread_mutex_unlock(&s->cmd_mutex);
    return 0;
}

int raop_session_resume(raop_session_t *s)
{
    if (!s || s->state != RAOP_STATE_PAUSED) return -1;
    pthread_mutex_lock(&s->cmd_mutex);
    s->pending_cmd.type = RAOP_CMD_RESUME;
    pthread_mutex_unlock(&s->cmd_mutex);
    return 0;
}

int raop_session_seek(raop_session_t *s, uint32_t position_ms)
{
    if (!s || (s->state != RAOP_STATE_STREAMING && s->state != RAOP_STATE_PAUSED))
        return -1;
    pthread_mutex_lock(&s->cmd_mutex);
    s->pending_cmd.type = RAOP_CMD_SEEK;
    s->pending_cmd.seek_position_ms = position_ms;
    pthread_mutex_unlock(&s->cmd_mutex);
    return 0;
}

int raop_session_set_volume(raop_session_t *s, int volume_pct)
{
    if (!s || (s->state != RAOP_STATE_STREAMING && s->state != RAOP_STATE_PAUSED
               && s->state != RAOP_STATE_IDLE))
        return -1;
    if (volume_pct < 0) volume_pct = 0;
    if (volume_pct > 100) volume_pct = 100;
    double vol_db = (volume_pct == 0) ? -144.0 : -30.0 * (1.0 - volume_pct / 100.0);
    pthread_mutex_lock(&s->cmd_mutex);
    s->pending_cmd.type = RAOP_CMD_VOLUME;
    s->pending_cmd.volume_db = vol_db;
    pthread_mutex_unlock(&s->cmd_mutex);
    return 0;
}

static void raop_cmd_free_metadata(struct raop_cmd *cmd)
{
    free((void *)cmd->metadata.title);
    free((void *)cmd->metadata.artist);
    free((void *)cmd->metadata.album);
    free((void *)cmd->metadata.genre);
    memset(&cmd->metadata, 0, sizeof(cmd->metadata));
}

int raop_session_set_metadata(raop_session_t *s, const raop_metadata_t *meta)
{
    if (!s || !meta || (s->state != RAOP_STATE_STREAMING && s->state != RAOP_STATE_PAUSED))
        return -1;
    pthread_mutex_lock(&s->cmd_mutex);
    raop_cmd_free_metadata(&s->pending_cmd);
    s->pending_cmd.type = RAOP_CMD_METADATA;
    s->pending_cmd.metadata.title = meta->title ? strdup(meta->title) : NULL;
    s->pending_cmd.metadata.artist = meta->artist ? strdup(meta->artist) : NULL;
    s->pending_cmd.metadata.album = meta->album ? strdup(meta->album) : NULL;
    s->pending_cmd.metadata.genre = meta->genre ? strdup(meta->genre) : NULL;
    s->pending_cmd.metadata.duration_ms = meta->duration_ms;
    s->pending_cmd.metadata.track_number = meta->track_number;
    s->pending_cmd.metadata.disc_number = meta->disc_number;
    pthread_mutex_unlock(&s->cmd_mutex);
    return 0;
}

int raop_session_set_artwork(raop_session_t *s, const uint8_t *data, size_t len, int is_png)
{
    if (!s || !data || len == 0 ||
        (s->state != RAOP_STATE_STREAMING && s->state != RAOP_STATE_PAUSED))
        return -1;
    uint8_t *copy = malloc(len);
    if (!copy) return -1;
    memcpy(copy, data, len);

    pthread_mutex_lock(&s->cmd_mutex);
    free(s->pending_cmd.artwork_data);
    s->pending_cmd.type = RAOP_CMD_ARTWORK;
    s->pending_cmd.artwork_data = copy;
    s->pending_cmd.artwork_len = len;
    s->pending_cmd.artwork_is_png = is_png;
    pthread_mutex_unlock(&s->cmd_mutex);
    return 0;
}

raop_state_t raop_session_get_state(const raop_session_t *s)
{
    if (!s) return RAOP_STATE_STOPPED;
    return s->state;
}

uint32_t raop_session_get_position_ms(const raop_session_t *s)
{
    if (!s || !s->rtp) return 0;
    uint32_t ts = rtp_stream_get_timestamp(s->rtp);
    return (uint32_t)((uint64_t)(ts - s->start_rtptime) * 1000ULL / SAMPLE_RATE);
}

uint32_t raop_session_get_duration_ms(const raop_session_t *s)
{
    if (!s) return 0;
    return (uint32_t)((uint64_t)s->total_samples * 1000ULL / SAMPLE_RATE);
}

int raop_session_get_volume_pct(const raop_session_t *s)
{
    if (!s) return 50;
    double db = s->current_volume_db;
    if (db <= -144.0) return 0;
    /* inverse of: db = -30.0 * (1.0 - pct/100.0) */
    int pct = (int)(100.0 * (1.0 + db / 30.0) + 0.5);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

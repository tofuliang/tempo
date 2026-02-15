#include "rtp_stream.h"
#include "../pal/pal.h"
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define RTP_VERSION 2
#define RTP_PAYLOAD_TYPE 96
#define RTP_HEADER_SIZE 12

typedef struct rtp_header {
    uint8_t vpxcc;
    uint8_t mpt;
    uint16_t seq;
    uint32_t timestamp;
    uint32_t ssrc;
} rtp_header_t;

struct rtp_stream {
    pal_socket_t *audio_socket;
    pal_socket_t *control_socket;
    char dest_host[256];
    uint16_t local_audio_port;
    uint16_t local_control_port;
    uint16_t local_timing_port;
    uint16_t remote_audio_port;
    uint16_t remote_control_port;
    uint16_t seq_num;
    uint32_t timestamp;
    uint32_t ssrc;
    int remote_ports_set;
    int first_pkt_sent;
    int audio_fd;
    int control_fd;
    struct sockaddr_in audio_dest;
    struct sockaddr_in control_dest;
};

static void rtp_stream_close_sockets(rtp_stream_t *stream)
{
    if (stream->audio_socket) {
        pal_socket_close(stream->audio_socket);
        stream->audio_socket = NULL;
    }

    if (stream->control_socket) {
        pal_socket_close(stream->control_socket);
        stream->control_socket = NULL;
    }


}

static int rtp_stream_bind_socket(pal_socket_t *socket, uint16_t *out_port)
{
    int ret = pal_socket_bind(socket, NULL, 0);
    if (ret < 0) {
        return -1;
    }

    if (pal_socket_get_local_addr(socket, NULL, 0, out_port) < 0) {
        return -1;
    }

    return 0;
}

static int rtp_stream_init_ssrc(rtp_stream_t *stream)
{
    stream->ssrc = (uint32_t)rand();
    stream->ssrc = (stream->ssrc << 16) ^ (uint32_t)rand();
    return 0;
}

rtp_stream_t *rtp_stream_create(const char *host,
                                uint16_t *local_audio_port,
                                uint16_t *local_control_port,
                                uint16_t *local_timing_port)
{
    rtp_stream_t *stream;

    if (!host || host[0] == '\0' || !local_audio_port || !local_control_port || !local_timing_port) {
        return NULL;
    }

    stream = calloc(1, sizeof(rtp_stream_t));
    if (!stream) {
        return NULL;
    }

    strncpy(stream->dest_host, host, sizeof(stream->dest_host) - 1);
    stream->dest_host[sizeof(stream->dest_host) - 1] = '\0';

    stream->audio_socket = pal_socket_create(PAL_SOCKET_UDP);
    stream->control_socket = pal_socket_create(PAL_SOCKET_UDP);
    if (!stream->audio_socket || !stream->control_socket) {
        rtp_stream_close_sockets(stream);
        free(stream);
        return NULL;
    }

    if (rtp_stream_bind_socket(stream->audio_socket, &stream->local_audio_port) < 0 ||
        rtp_stream_bind_socket(stream->control_socket, &stream->local_control_port) < 0) {
        rtp_stream_close_sockets(stream);
        free(stream);
        return NULL;
    }

    if (rtp_stream_init_ssrc(stream) < 0) {
        rtp_stream_close_sockets(stream);
        free(stream);
        return NULL;
    }

    stream->seq_num = 0;
    stream->timestamp = 0;
    stream->remote_ports_set = 0;
    stream->first_pkt_sent = 0;

    *local_audio_port = stream->local_audio_port;
    *local_control_port = stream->local_control_port;
    *local_timing_port = 0;

    return stream;
}

void rtp_stream_destroy(rtp_stream_t *stream)
{
    if (!stream) {
        return;
    }

    rtp_stream_close_sockets(stream);
    free(stream);
}

int rtp_stream_set_server_ports(rtp_stream_t *stream,
                                uint16_t audio_port,
                                uint16_t control_port,
                                uint16_t timing_port)
{
    if (!stream || audio_port == 0 || control_port == 0 || timing_port == 0) {
        return -1;
    }

    stream->remote_audio_port = audio_port;
    stream->remote_control_port = control_port;

    memset(&stream->audio_dest, 0, sizeof(stream->audio_dest));
    stream->audio_dest.sin_family = AF_INET;
    stream->audio_dest.sin_port = htons(audio_port);
    inet_pton(AF_INET, stream->dest_host, &stream->audio_dest.sin_addr);

    memset(&stream->control_dest, 0, sizeof(stream->control_dest));
    stream->control_dest.sin_family = AF_INET;
    stream->control_dest.sin_port = htons(control_port);
    inet_pton(AF_INET, stream->dest_host, &stream->control_dest.sin_addr);

    stream->audio_fd = pal_socket_get_fd(stream->audio_socket);
    stream->control_fd = pal_socket_get_fd(stream->control_socket);

    stream->remote_ports_set = 1;
    return 0;
}

int rtp_stream_send_audio(rtp_stream_t *stream, const uint8_t *data, size_t len)
{
    uint8_t packet[2048];
    rtp_header_t header;
    int sent;

    if (!stream || !data || len == 0 || !stream->remote_ports_set) {
        return -1;
    }

    if (len + RTP_HEADER_SIZE > sizeof(packet)) {
        return -1;
    }

    header.vpxcc = (uint8_t)(RTP_VERSION << 6);
    /* Marker bit: M=1 only on first packet, M=0 for all subsequent */
    if (!stream->first_pkt_sent) {
        header.mpt = (uint8_t)(0x80 | RTP_PAYLOAD_TYPE);
        stream->first_pkt_sent = 1;
    } else {
        header.mpt = (uint8_t)(RTP_PAYLOAD_TYPE);
    }
    header.seq = htons(stream->seq_num);
    header.timestamp = htonl(stream->timestamp);
    header.ssrc = htonl(stream->ssrc);

    memcpy(packet, &header, RTP_HEADER_SIZE);
    memcpy(packet + RTP_HEADER_SIZE, data, len);

    sent = (int)sendto(stream->audio_fd, packet, RTP_HEADER_SIZE + len, 0,
                       (struct sockaddr *)&stream->audio_dest,
                       sizeof(stream->audio_dest));
    if (sent < 0) {
        return -1;
    }

    stream->seq_num++;
    stream->timestamp += 352;

    return 0;
}

uint16_t rtp_stream_get_seq(const rtp_stream_t *stream)
{
    if (!stream) {
        return 0;
    }

    return stream->seq_num;
}

uint32_t rtp_stream_get_timestamp(const rtp_stream_t *stream)
{
    if (!stream) {
        return 0;
    }

    return stream->timestamp;
}

int rtp_stream_send_control(rtp_stream_t *stream, const uint8_t *data, size_t len)
{
    if (!stream || !data || len == 0 || !stream->remote_ports_set)
        return -1;

    return (int)sendto(stream->control_fd, data, len, 0,
                       (struct sockaddr *)&stream->control_dest,
                       sizeof(stream->control_dest));
}

void rtp_stream_set_position(rtp_stream_t *stream, uint16_t seq, uint32_t timestamp)
{
    if (!stream) return;
    stream->seq_num = seq;
    stream->timestamp = timestamp;
    stream->first_pkt_sent = 0;
}

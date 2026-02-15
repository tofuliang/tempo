#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/core/rtp_stream.h"

static void test_rtp_stream_create_returns_ports(void)
{
    uint16_t audio_port = 0;
    uint16_t control_port = 0;
    uint16_t timing_port = 0;

    rtp_stream_t *stream = rtp_stream_create("127.0.0.1",
                                             &audio_port,
                                             &control_port,
                                             &timing_port);
    assert(stream != NULL);

    assert(audio_port != 0);
    assert(control_port != 0);
    assert(timing_port != 0);

    rtp_stream_destroy(stream);
    printf("\u2713 test_rtp_stream_create_returns_ports\n");
}

static void test_rtp_stream_set_server_ports_rejects_invalid(void)
{
    uint16_t audio_port = 0;
    uint16_t control_port = 0;
    uint16_t timing_port = 0;

    rtp_stream_t *stream = rtp_stream_create("127.0.0.1",
                                             &audio_port,
                                             &control_port,
                                             &timing_port);
    assert(stream != NULL);

    int ret = rtp_stream_set_server_ports(stream, 0, 6000, 6001);
    assert(ret != 0);

    ret = rtp_stream_set_server_ports(stream, 6000, 0, 6001);
    assert(ret != 0);

    ret = rtp_stream_set_server_ports(stream, 6000, 6001, 0);
    assert(ret != 0);

    rtp_stream_destroy(stream);
    printf("\u2713 test_rtp_stream_set_server_ports_rejects_invalid\n");
}

static void test_rtp_stream_send_audio_requires_server_ports(void)
{
    uint16_t audio_port = 0;
    uint16_t control_port = 0;
    uint16_t timing_port = 0;

    rtp_stream_t *stream = rtp_stream_create("127.0.0.1",
                                             &audio_port,
                                             &control_port,
                                             &timing_port);
    assert(stream != NULL);

    uint8_t data[16] = {0};
    int ret = rtp_stream_send_audio(stream, data, sizeof(data));
    assert(ret != 0);

    rtp_stream_destroy(stream);
    printf("\u2713 test_rtp_stream_send_audio_requires_server_ports\n");
}

int main(void)
{
    test_rtp_stream_create_returns_ports();
    test_rtp_stream_set_server_ports_rejects_invalid();
    test_rtp_stream_send_audio_requires_server_ports();

    printf("\n\u2713 All rtp_stream tests passed!\n");
    return 0;
}

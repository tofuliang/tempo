#ifndef RTP_STREAM_H
#define RTP_STREAM_H

#include <stdint.h>
#include <stddef.h>

/**
 * RTP Streaming Module
 * 
 * Provides RTP packet creation and streaming for RAOP audio transport.
 * Uses UDP via PAL network layer for sending audio packets.
 */

/* Opaque RTP stream */
typedef struct rtp_stream rtp_stream_t;

/**
 * Create new RTP stream context
 *
 * Creates UDP sockets for audio, control, and timing and binds them
 * to local ports.
 *
 * @param host Destination hostname or IP
 * @param local_audio_port Output local audio port
 * @param local_control_port Output local control port
 * @param local_timing_port Output local timing port
 * @return RTP stream or NULL on failure
 */
rtp_stream_t *rtp_stream_create(const char *host,
                                uint16_t *local_audio_port,
                                uint16_t *local_control_port,
                                uint16_t *local_timing_port);

/**
 * Destroy RTP stream and close sockets
 *
 * @param stream RTP stream
 */
void rtp_stream_destroy(rtp_stream_t *stream);

/**
 * Set destination ports for RTP packets
 *
 * @param stream RTP stream
 * @param audio_port Remote audio port
 * @param control_port Remote control port
 * @param timing_port Remote timing port
 * @return 0 on success, -1 on failure
 */
int rtp_stream_set_server_ports(rtp_stream_t *stream,
                                uint16_t audio_port,
                                uint16_t control_port,
                                uint16_t timing_port);

/**
 * Send audio data as RTP packet
 *
 * Encapsulates audio data in RTP packet with proper headers,
 * sequence numbers, and timestamps. Sends via UDP.
 *
 * @param stream RTP stream
 * @param data Audio data to send
 * @param len Length of audio data
 * @return 0 on success, -1 on failure
 */
int rtp_stream_send_audio(rtp_stream_t *stream, const uint8_t *data, size_t len);

/**
 * Get current RTP sequence number
 *
 * @param stream RTP stream
 * @return Current sequence number
 */
uint16_t rtp_stream_get_seq(const rtp_stream_t *stream);

/**
 * Get current RTP timestamp
 *
 * @param stream RTP stream
 * @return Current timestamp
 */
uint32_t rtp_stream_get_timestamp(const rtp_stream_t *stream);

/**
 * Send raw data via control socket to remote control port
 */
int rtp_stream_send_control(rtp_stream_t *stream, const uint8_t *data, size_t len);

/**
 * Reset sequence number and timestamp for seek/resume operations
 */
void rtp_stream_set_position(rtp_stream_t *stream, uint16_t seq, uint32_t timestamp);

#endif /* RTP_STREAM_H */

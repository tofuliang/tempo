# AirPlay 2 Support Design Document

**Date**: 2026-01-01
**Author**: Design Discussion with User
**Status**: Approved for Implementation

## Overview

### Objective
Add AirPlay 2 sender functionality to Tempo, enabling users to stream music from their Subsonic server to AirPlay devices (Apple TV, HomePod, AirPlay-enabled speakers, etc.).

### Scope
- AirPlay 2 protocol implementation from scratch (referencing Owntone's C implementation)
- Single device playback with multi-room expansion points
- Complete feature set: discovery, connection, playback, metadata, volume control, encryption
- Smart reconnection and automatic fallback to local playback

### Non-Goals
- AirPlay 1 only (not implemented)
- Acting as an AirPlay receiver (that's Owntone's domain)
- Multi-room audio in phase 1 (architecture will support it)

## Core Features

### Must Have
1. **Device Discovery**: mDNS/DNS-SD with intelligent scanning and caching
2. **Connection Management**: Both encrypted and unencrypted connections
3. **Audio Streaming**: Smart transcoding (ALAC passthrough, AAC/ALAC/PCM encoding)
4. **Playback Control**: Play, pause, seek, volume, queue management
5. **Metadata**: Full DMAP metadata (title, artist, album, artwork, progress)
6. **Error Handling**: Smart reconnection, automatic fallback to local playback

### Should Have
1. **Capability Negotiation**: Query device capabilities before sending metadata
2. **Memory Management**: Efficient artwork caching and audio buffer pooling
3. **Power Awareness**: Reduced scanning frequency in low-battery situations

### Could Have
1. **Multi-room Audio**: Phase 2 feature
2. **Advanced Metadata**: Lyrics, composer information

## Architecture

### Component Structure

```
app/src/tempo/java/com/cappielloantonio/tempo/
├── player/
│   └── AirPlayPlayer.kt              # Media3 Player interface implementation
├── service/
│   ├── AirPlayDeviceScanner.kt       # mDNS device discovery
│   ├── AirPlayConnectionManager.kt   # Connection lifecycle management
│   ├── AirPlayAudioPipeline.kt       # Audio encoding & RTP transmission
│   └── CapabilityNegotiator.kt       # Device capability queries
├── protocol/
│   ├── rtsp/
│   │   ├── RTSPClient.kt             # RTSP protocol client
│   │   └── RTSPRequest.kt            # RTSP request/response handling
│   ├── rtp/
│   │   ├── RTPSession.kt             # RTP session management
│   │   └── RTPPacket.kt              # RTP packet construction
│   ├── encryption/
│   │   ├── PairVerify.kt             # Pair-Verify authentication
│   │   ├── PairSetup.kt              # Pair-Setup key exchange
│   │   └── AESEncryptor.kt           # AES-CTR encryption
│   └── metadata/
│       ├── DMAPBuilder.kt            # DMAP metadata encoding
│       └── MetadataSender.kt         # Metadata transmission
└── ui/
    ├── dialog/
    │   └── AirPlayDeviceDialog.kt    # Device selection bottom sheet
    └── preference/
        └── AirPlayPreferences.kt     # Settings for AirPlay
```

### Data Flow

```
Subsonic Server
    ↓
ExoPlayer (local decode)
    ↓
AirPlayPlayer (encoding)
    ↓
AirPlayConnectionManager
    ↓
RTP/AirPlay Protocol Stack
    ↓
AirPlay Device
```

### Integration with Existing Code

**MediaService.kt** modifications:
- Add `AirPlayPlayer` alongside `ExoPlayer` and `CastPlayer`
- Implement player switching logic (local ↔ AirPlay)
- Add connection state listeners

**PlayerFragment.kt** modifications:
- Add AirPlay cast button (similar to Chromecast button)
- Show connection status and device name
- Handle disconnection

## Detailed Design

### 1. Device Discovery

**Technology**: Android NsdManager with mDNS/DNS-SD

**Service Types**:
- `_airplay._tcp.` (AirPlay 2)
- `_airplay-2._tcp.` (optional, enhanced discovery)

**Device Information**:
```kotlin
data class AirPlayDevice(
    val name: String,
    val host: String,
    val port: Int,
    val deviceId: String,
    val features: Int,              // Feature flags
    val model: String,
    val version: Int,
    val supportsEncryption: Boolean,
    val lastSeen: Long
)
```

**Scanning Strategy**:
- **Active**: Wi-Fi + app foreground → scan every 30 seconds
- **Passive**: Wi-Fi + app background → scan every 2 minutes
- **Suspended**: Mobile network → stop scanning

**Caching**:
- Devices cached for 30 minutes
- Periodic validation (ping to check availability)
- User-connected devices cached longer (24 hours)

### 2. Connection Management

**Connection Flow**:
1. TCP connection establishment (typically port 7000)
2. RTSP handshake (OPTIONS request)
3. Device authentication (if encrypted):
   - Pair-Setup: SRP key exchange
   - Pair-Verify: ED25519 signature verification
   - Session key generation
4. Stream parameter negotiation
5. RTP session establishment

**Authentication**:
- **Unencrypted**: Simple RTSP handshake
- **Encrypted**: Full Pair-Verify + Pair-Setup with SRP and ED25519

**Reconnection Strategy**:
- **Network error**: Auto-reconnect with exponential backoff (3 attempts)
- **Device offline**: Notify user, stop auto-reconnect
- **Authentication failed**: Prompt user to check password/device settings

**Automatic Fallback**:
- On disconnection → seamlessly switch to local ExoPlayer
- Preserve playback position during transition

### 3. Audio Processing

**Encoding Decision Tree**:
```
Source Format → Device Capability → Encoding
ALAC         → Any               → PASS_THROUGH
Compressed   → Supports AAC      → AAC
Lossless     → Supports ALAC     → ALAC
Any          → Default           → PCM 16-bit 44.1kHz
```

**Audio Pipeline**:
1. Extract PCM from ExoPlayer
2. Encode (AAC/ALAC/PCM) based on negotiation
3. Encrypt (AES-CTR) if required
4. Wrap in RTP packets
5. Transmit via UDP

**RTP Parameters**:
- Payload type: 0x60
- Sample rate: 44100 Hz (default)
- Channels: 2 (stereo)
- Bits per sample: 16
- Samples per packet: 352
- Latency: 250ms

**Time Synchronization**:
- NTP-based clock synchronization
- Timestamp calculation based on RTP sequence number
- Compensation for network latency

### 4. Metadata Handling

**Capability Query**:
Query device capabilities before sending metadata:
```kotlin
data class AirPlayDeviceCapabilities(
    val supportsMetadata: Boolean,
    val supportsArtwork: Boolean,
    val maxArtworkSize: Int?,
    val supportedArtworkFormats: List<String>,
    val supportsProgress: Boolean,
    val supportsVolumeControl: Boolean,
    val dmapVersion: Int?,
    val supportedFeatures: Int
)
```

**Metadata Format** (DMAP):
- Item ID, container ID
- Title, artist, album, genre
- Duration, current position
- Artwork availability flag

**Artwork Processing**:
1. Load from Subsonic or local cache
2. Encode to supported format (PNG/JPEG)
3. Scale to maximum supported size (default 1024x1024)
4. Base64 encode and transmit via RTSP SET_PARAMETER

**Progress Updates**:
- Sent every 1 second during playback
- Includes duration and current position
- Format: DMAP floating-point seconds

### 5. AirPlayPlayer Implementation

**Media3 Player Interface**:
- `play()`: Send RTSP PLAY command
- `pause()`: Send RTSP PAUSE command
- `seekTo(positionMs)`: Send RTSP PLAY with Range header
- `setDeviceVolume(volume)`: Send RTSP SET_PARAMETER for volume
- `getDeviceVolume()`: Query current device volume

**State Management**:
```kotlin
sealed class AirPlayState {
    object Idle
    object Connecting
    data class Connected(val device: AirPlayDevice)
    data class Playing(val positionMs: Long)
    object Paused
    data class Error(val message: String)
}
```

**Player Switching**:
```kotlin
// In MediaService
fun switchToAirPlay(device: AirPlayDevice) {
    connect(device).onSuccess { session ->
        setPlayer(mediaLibrarySession.player, airPlayPlayer)
    }
}

fun switchToLocal() {
    setPlayer(airPlayPlayer, player)
}
```

### 6. UI Integration

**Cast Button**:
- Added to playback control bar (alongside Chromecast button)
- Visible when AirPlay devices are available
- Opens device selection bottom sheet

**Device Selection Dialog**:
- Lists discovered AirPlay devices
- Shows connection status
- Displays encryption status (🔒 or 🔓)
- Shows signal strength
- "Disconnect" option when connected

**Connection Status**:
- In-app toast/snackbar notifications
- Persistent indicator when connected
- Loading animation during connection

**Settings**:
- Enable/disable AirPlay
- Scan frequency (power saving / normal / high performance)
- Clear device cache
- Debug logging toggle

### 7. Error Handling

**Error Categories**:
- Network: Device not found, timeout, network unavailable
- Authentication: Failed auth, encryption error
- Protocol: RTSP errors, unsupported features
- Audio: Encoding errors, stream interruption
- Device: Disconnected, busy

**Error Response**:
- User-friendly messages
- Retry options where applicable
- Automatic fallback to local playback
- Detailed logging for debugging

### 8. Performance Optimization

**Memory Management**:
- LRU cache for artwork (1/8 of max heap)
- Object pooling for audio buffers
- Proper cleanup on disconnection

**Network Optimization**:
- TCP connection pooling (max 3 concurrent)
- Connection reuse for same device
- Smart reconnection to avoid storms

**Power Optimization**:
- Reduced scan frequency in low battery
- Stop scanning in power save mode
- Efficient coroutine dispatchers

**Thread Management**:
- Dedicated dispatcher for device discovery
- Dedicated dispatcher for connection establishment
- High-priority dispatcher for audio processing

**Startup Optimization**:
- Core features initialized immediately
- Auxiliary components lazy-loaded
- Background initialization for non-critical paths

## Implementation Plan

### Phase 1: Protocol Foundation (2-3 weeks)
- [ ] Implement mDNS/DNS-SD device discovery
- [ ] RTSP protocol stack basics
- [ ] Device capability queries
- [ ] Unit test framework setup

### Phase 2: Connection & Authentication (2-3 weeks)
- [ ] TCP connection management
- [ ] Unencrypted connections
- [ ] Encrypted connections (Pair-Verify/Pair-Setup)
- [ ] Smart reconnection mechanism
- [ ] Connection state management

### Phase 3: Audio Streaming (3-4 weeks)
- [ ] RTP protocol implementation
- [ ] Audio encoders (AAC/ALAC/PCM)
- [ ] AES encryption
- [ ] Time synchronization
- [ ] Buffer management

### Phase 4: Metadata & UI (2 weeks)
- [ ] DMAP metadata encoding
- [ ] Artwork transmission
- [ ] Progress synchronization
- [ ] Device selection dialog
- [ ] Cast button integration
- [ ] Settings page

### Phase 5: Integration & Optimization (2 weeks)
- [ ] MediaService integration
- [ ] ExoPlayer integration
- [ ] Error handling polish
- [ ] Performance optimization
- [ ] Memory leak checks

### Phase 6: Testing (1-2 weeks)
- [ ] Real device testing (various AirPlay devices)
- [ ] Network scenario testing
- [ ] Stress testing
- [ ] Bug fixing

**Total Estimated Time**: 12-16 weeks

## Testing Strategy

### Unit Tests
- RTSP request/response encoding
- RTP packet construction
- Audio encoding/decoding
- Device discovery parsing
- Metadata DMAP encoding

### Integration Tests
- End-to-end connection flow
- Full playback pipeline
- Error recovery scenarios
- State machine transitions

### Manual Testing
- Real AirPlay devices (Apple TV, HomePod, third-party speakers)
- Network conditions (Wi-Fi, weak signal, roaming)
- Battery scenarios
- Various audio formats

### Performance Tests
- Memory usage profiling
- CPU usage profiling
- Battery drain measurement
- Network bandwidth analysis

## Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| AirPlay 2 protocol complexity higher than expected | High | Implement core features first, add advanced features later |
| Encryption implementation challenges | Medium | Reference Owntone implementation, use mature crypto libraries |
| Device compatibility issues | Medium | Test on diverse devices, gather user feedback |
| Performance/battery issues | Medium | Profiling, optimize based on data |
| mDNS behavior varies across Android versions | Low | Use compatibility libraries, graceful degradation |

## References

- **Owntone Source Code**: `/Users/tofu/Dev/github.com/owntone/src/outputs/`
  - `airplay.c`: Main AirPlay 2 implementation
  - `raop.c`: RAOP (RealTime Audio Output Protocol)
  - `airplay_events.c`: Event handling
- **Media3 Documentation**: https://developer.android.com/guide/topics/media/media3
- **Android NsdManager**: https://developer.android.com/guide/topics/connectivity/nsd
- **AirPlay Protocol Specifications**: Reverse-engineered from open-source implementations

## Success Criteria

- [ ] Can discover and list AirPlay devices on the network
- [ ] Can connect to unencrypted AirPlay devices
- [ ] Can connect to encrypted AirPlay devices
- [ ] Can play audio streams without stuttering
- [ ] Can display album artwork and metadata
- [ ] Can control volume independently
- [ ] Automatically reconnection on network hiccups
- [ ] Seamlessly falls back to local playback on disconnect
- [ ] Passes all automated tests
- [ ] Works on at least 3 different AirPlay device types

## Future Enhancements

- Multi-room audio (play to multiple devices simultaneously)
- AirPlay 1 backward compatibility
- Advanced metadata (lyrics, composer)
- Video streaming support
- AirPlay receiver mode (make Tempo itself an AirPlay destination)

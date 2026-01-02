package com.cappielloantonio.tempo.protocol.rtsp

data class RTSPRequest(
    val method: RTSPMethod,
    val uri: String,
    val headers: Map<String, String> = emptyMap(),
    val body: ByteArray? = null,
    val sequence: Int = 0,
    val sessionId: String? = null
) {
    enum class RTSPMethod(val value: String) {
        OPTIONS("OPTIONS"),
        ANNOUNCE("ANNOUNCE"),
        DESCRIBE("DESCRIBE"),
        SETUP("SETUP"),
        RECORD("RECORD"),
        PLAY("PLAY"),
        PAUSE("PAUSE"),
        TEARDOWN("TEARDOWN"),
        GET_PARAMETER("GET_PARAMETER"),
        SET_PARAMETER("SET_PARAMETER")
    }

    fun toByteArray(): ByteArray {
        val lines = buildList {
            add("$method $uri RTSP/1.0")
            add("CSeq: $sequence")
            sessionId?.let { add("Session: $it") }
            // Validate headers don't contain newlines
            headers.forEach { (key, value) ->
                if (!key.contains("\r") && !key.contains("\n") &&
                    !value.contains("\r") && !value.contains("\n")) {
                    add("$key: $value")
                }
            }
            if (body != null) add("Content-Length: ${body.size}")
            add("")
        }

        val header = lines.joinToString("\r\n")
        return if (body != null) {
            "$header\r\n".toByteArray() + body
        } else {
            "$header\r\n".toByteArray()
        }
    }

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as RTSPRequest
        return method == other.method && uri == other.uri
    }

    override fun hashCode(): Int {
        var result = method.hashCode()
        result = 31 * result + uri.hashCode()
        return result
    }
}

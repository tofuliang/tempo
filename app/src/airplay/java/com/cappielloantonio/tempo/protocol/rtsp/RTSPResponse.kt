package com.cappielloantonio.tempo.protocol.rtsp

data class RTSPResponse(
    val statusCode: Int,
    val statusText: String,
    val headers: Map<String, String> = emptyMap(),
    val body: ByteArray? = null
) {
    val isSuccess: Boolean
        get() = statusCode in 200..299

    val sessionId: String?
        get() = headers["Session"]

    companion object {
        fun fromByteArray(data: ByteArray): RTSPResponse? {
            return try {
                val text = String(data)
                val lines = text.split("\r\n")

                // Parse status line with bounds checking
                if (lines.isEmpty()) return null
                val statusLine = lines[0]
                val parts = statusLine.split(" ", limit = 3)
                if (parts.size < 3) return null

                val statusCode = parts[1].toIntOrNull() ?: return null
                val statusText = parts[2]

                // Parse headers safely
                val headers = mutableMapOf<String, String>()
                var bodyStart = 1

                for (i in 1 until lines.size) {
                    if (lines[i].isEmpty()) {
                        bodyStart = i + 1
                        break
                    }

                    // Safe header parsing
                    val colonIndex = lines[i].indexOf(": ")
                    if (colonIndex > 0 && colonIndex < lines[i].length - 2) {
                        val key = lines[i].substring(0, colonIndex)
                        val value = lines[i].substring(colonIndex + 2)
                        headers[key] = value
                    }
                    // Skip malformed headers silently
                }

                // Extract body
                val body = if (bodyStart < lines.size && headers.containsKey("Content-Length")) {
                    val contentLength = headers["Content-Length"]?.toIntOrNull() ?: return null
                    if (contentLength <= 0) return null

                    val totalHeaderLength = text.indexOf("\r\n\r\n") + 4
                    if (totalHeaderLength + contentLength > data.size) return null

                    data.sliceArray(totalHeaderLength until totalHeaderLength + contentLength)
                } else null

                RTSPResponse(statusCode, statusText, headers, body)
            } catch (e: Exception) {
                null
            }
        }
    }

    // Note: body ByteArray uses reference equality, not content comparison
    // This is intentional for performance - bodies can be large
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as RTSPResponse
        return statusCode == other.statusCode
    }

    override fun hashCode(): Int {
        return statusCode
    }
}

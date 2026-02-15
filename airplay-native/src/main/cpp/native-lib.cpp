#include <jni.h>
#include <string>
#include <android/log.h>
#include "raop.h"

#define TAG "AirPlayJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static raop_session_t* session = nullptr;
static JavaVM* g_jvm = nullptr;
static jobject g_callback = nullptr;

static void state_callback(raop_session_t* /*session*/, raop_state_t state, void* /*user_data*/) {
    if (!g_jvm || !g_callback) return;

    JNIEnv* env = nullptr;
    bool attached = false;
    int getEnvStat = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvStat == JNI_EDETACHED) {
        g_jvm->AttachCurrentThread(&env, nullptr);
        attached = true;
    }
    if (!env) return;

    jclass cls = env->GetObjectClass(g_callback);
    jmethodID mid = env->GetMethodID(cls, "onStateChanged", "(I)V");
    if (mid) {
        env->CallVoidMethod(g_callback, mid, (jint)state);
    }
    env->DeleteLocalRef(cls);

    if (attached) {
        g_jvm->DetachCurrentThread();
    }
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cappielloantonio_tempo_airplay_AirPlayClient_connectAirPlay(
        JNIEnv* env, jobject, jstring jIp, jint jPort, jobject callback) {

    if (session) {
        raop_state_t st = raop_session_get_state(session);
        LOGI("connectAirPlay: stale session (state=%d), cleaning up", st);
        raop_session_stop(session);
        raop_session_free(session);
        session = nullptr;
        if (g_callback) {
            env->DeleteGlobalRef(g_callback);
            g_callback = nullptr;
        }
    }

    const char* ip = env->GetStringUTFChars(jIp, nullptr);
    LOGI("Connecting to AirPlay device %s:%d", ip, jPort);

    if (g_callback) {
        env->DeleteGlobalRef(g_callback);
        g_callback = nullptr;
    }
    if (callback) {
        g_callback = env->NewGlobalRef(callback);
    }

    session = raop_session_new(ip, (uint16_t)jPort, state_callback, nullptr);
    if (!session) {
        LOGE("Failed to create session");
        env->ReleaseStringUTFChars(jIp, ip);
        return -1;
    }

    int ret = raop_session_connect(session);
    env->ReleaseStringUTFChars(jIp, ip);
    return ret;
}

extern "C" JNIEXPORT void JNICALL
Java_com_cappielloantonio_tempo_airplay_AirPlayClient_stopAirPlay(
        JNIEnv* env, jobject) {
    if (session) {
        LOGI("Stopping AirPlay session");
        raop_session_stop(session);
        raop_session_free(session);
        session = nullptr;
    }
    if (g_callback) {
        env->DeleteGlobalRef(g_callback);
        g_callback = nullptr;
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cappielloantonio_tempo_airplay_AirPlayClient_pauseAirPlay(
        JNIEnv*, jobject) {
    if (!session) { LOGE("No active session"); return -1; }
    LOGI("Pausing");
    return raop_session_pause(session);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cappielloantonio_tempo_airplay_AirPlayClient_resumeAirPlay(
        JNIEnv*, jobject) {
    if (!session) { LOGE("No active session"); return -1; }
    LOGI("Resuming");
    return raop_session_resume(session);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cappielloantonio_tempo_airplay_AirPlayClient_seekAirPlay(
        JNIEnv*, jobject, jint positionMs) {
    if (!session) { LOGE("No active session"); return -1; }
    LOGI("Seeking to %d ms", positionMs);
    return raop_session_seek(session, (uint32_t)positionMs);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cappielloantonio_tempo_airplay_AirPlayClient_setVolume(
        JNIEnv*, jobject, jint volumePct) {
    if (!session) { LOGE("No active session"); return -1; }
    LOGI("Setting volume to %d%%", volumePct);
    return raop_session_set_volume(session, (int)volumePct);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cappielloantonio_tempo_airplay_AirPlayClient_setMetadata(
        JNIEnv* env, jobject,
        jstring jTitle, jstring jArtist, jstring jAlbum, jstring jGenre,
        jint durationMs, jint trackNumber, jint discNumber) {
    if (!session) { LOGE("No active session"); return -1; }

    const char* title  = jTitle  ? env->GetStringUTFChars(jTitle, nullptr)  : nullptr;
    const char* artist = jArtist ? env->GetStringUTFChars(jArtist, nullptr) : nullptr;
    const char* album  = jAlbum  ? env->GetStringUTFChars(jAlbum, nullptr)  : nullptr;
    const char* genre  = jGenre  ? env->GetStringUTFChars(jGenre, nullptr)  : nullptr;

    LOGI("Setting metadata: title=%s artist=%s album=%s",
         title ? title : "(null)", artist ? artist : "(null)", album ? album : "(null)");

    raop_metadata_t meta = {};
    meta.title        = title;
    meta.artist       = artist;
    meta.album        = album;
    meta.genre        = genre;
    meta.duration_ms  = (uint32_t)durationMs;
    meta.track_number = (uint32_t)trackNumber;
    meta.disc_number  = (uint32_t)discNumber;

    int ret = raop_session_set_metadata(session, &meta);

    if (title)  env->ReleaseStringUTFChars(jTitle, title);
    if (artist) env->ReleaseStringUTFChars(jArtist, artist);
    if (album)  env->ReleaseStringUTFChars(jAlbum, album);
    if (genre)  env->ReleaseStringUTFChars(jGenre, genre);

    return ret;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cappielloantonio_tempo_airplay_AirPlayClient_setArtwork(
        JNIEnv* env, jobject, jbyteArray jData, jboolean isPng) {
    if (!session) { LOGE("No active session"); return -1; }
    if (!jData) { LOGE("Artwork data is null"); return -1; }

    jsize len = env->GetArrayLength(jData);
    jbyte* data = env->GetByteArrayElements(jData, nullptr);

    LOGI("Setting artwork: %d bytes, isPng=%d", len, isPng);
    int ret = raop_session_set_artwork(session, (const uint8_t*)data, (size_t)len, isPng ? 1 : 0);

    env->ReleaseByteArrayElements(jData, data, JNI_ABORT);
    return ret;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cappielloantonio_tempo_airplay_AirPlayClient_getVolumePct(
        JNIEnv*, jobject) {
    if (!session) return 50;
    return (jint)raop_session_get_volume_pct(session);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cappielloantonio_tempo_airplay_AirPlayClient_getPositionMs(
        JNIEnv*, jobject) {
    if (!session) return -1;
    return (jint)raop_session_get_position_ms(session);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cappielloantonio_tempo_airplay_AirPlayClient_getDurationMs(
        JNIEnv*, jobject) {
    if (!session) return -1;
    return (jint)raop_session_get_duration_ms(session);
}

extern "C" JNIEXPORT void JNICALL
Java_com_cappielloantonio_tempo_airplay_AirPlayClient_nativeStartPushMode(
        JNIEnv*, jobject) {
    if (!session) { LOGE("No active session for startPushMode"); return; }
    LOGI("Starting push mode");
    raop_session_start_push(session);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_cappielloantonio_tempo_airplay_AirPlayClient_nativePushPcm(
        JNIEnv* env, jobject, jbyteArray pcmData, jint sampleCount) {
    if (!session) return -1;
    if (!pcmData) return -1;

    jbyte* data = env->GetByteArrayElements(pcmData, nullptr);
    int ret = raop_session_push_pcm(session, (const int16_t*)data, sampleCount);
    env->ReleaseByteArrayElements(pcmData, data, JNI_ABORT);
    return ret;
}

extern "C" JNIEXPORT void JNICALL
Java_com_cappielloantonio_tempo_airplay_AirPlayClient_nativeFlushPushBuffer(
        JNIEnv*, jobject) {
    if (!session) return;
    LOGI("Flushing push buffer");
    raop_session_flush_push_buffer(session);
}

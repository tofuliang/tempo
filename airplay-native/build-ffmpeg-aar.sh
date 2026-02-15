#!/bin/bash
#
# Build lib-decoder-ffmpeg.aar with ALAC support locally
# For ALAC audio extraction in AirPlay functionality
#

set -e

unset LDFLAGS CFLAGS CPPFLAGS PKG_CONFIG_PATH PKG_CONFIG_LIBDIR

# ==================== Color Output ====================
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# ==================== Configuration Parameters ====================

# Android NDK path (modify according to your environment)
if [ -z "$ANDROID_NDK_HOME" ]; then
    log_error "ANDROID_NDK_HOME environment variable is not set"
    log_error "Please set ANDROID_NDK_HOME or specify NDK path manually in the script"
    exit 1
fi

NDK_PATH="$ANDROID_NDK_HOME"
log_info "Using NDK: $NDK_PATH"

# Detect host platform
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    HOST_PLATFORM="linux-x86_64"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    HOST_PLATFORM="darwin-x86_64"
else
    log_error "Unsupported platform: $OSTYPE"
    exit 1
fi

log_info "Host platform: $HOST_PLATFORM"

ANDROID_ABI=21
WORK_DIR="$(pwd)/build_ffmpeg_temp"
ANDROIDX_MEDIA_VERSION="1.4.1"

# ==================== Cleanup Old Build ====================

cleanup() {
    log_info "Cleaning temporary files..."
    if [ -d "$WORK_DIR" ]; then
        rm -rf "$WORK_DIR"
    fi
}

# Set cleanup on exit (disable for debugging)
# trap cleanup EXIT

# ==================== Check Dependencies ====================

check_dependencies() {
    log_info "Checking dependency tools..."
    
    local missing_deps=()
    
    if ! command -v git &> /dev/null; then
        missing_deps+=("git")
    fi
    
    if ! command -v java &> /dev/null; then
        missing_deps+=("java (JDK 17)")
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        log_error "Missing dependencies: ${missing_deps[*]}"
        exit 1
    fi
    
    log_info "All dependencies satisfied"
}

# ==================== Download AndroidX Media ====================

download_androidx_media() {
    log_info "Downloading AndroidX Media $ANDROIDX_MEDIA_VERSION..."
    
    mkdir -p "$WORK_DIR"
    cd "$WORK_DIR"
    
    if [ ! -d "media" ]; then
        git clone --branch "$ANDROIDX_MEDIA_VERSION" --single-branch --depth 1 \
            https://github.com/androidx/media.git media
    else
        log_info "AndroidX Media already exists, skipping download"
    fi
    
    cd -
}

# ==================== Download FFmpeg ====================

download_ffmpeg() {
    log_info "Downloading FFmpeg 6.0..."
    
    cd "$WORK_DIR"
    
    if [ ! -d "ffmpeg" ]; then
        # Use HTTPS instead of git protocol to avoid firewall issues
        git clone --branch release/6.0 --single-branch --depth 1 \
            https://git.ffmpeg.org/ffmpeg.git ffmpeg
    else
        log_info "FFmpeg already exists, skipping download"
    fi
    
    cd -
}

# ==================== Download and Build mbedTLS ====================

MBEDTLS_VERSION="2.28.9"
MBEDTLS_DIR="$WORK_DIR/mbedtls"
MBEDTLS_INSTALL="$WORK_DIR/mbedtls_install"

download_mbedtls() {
    log_info "Downloading mbedTLS $MBEDTLS_VERSION..."

    cd "$WORK_DIR"

    if [ -d "mbedtls" ]; then
        local existing_ver=$(cd mbedtls && git describe --tags 2>/dev/null || echo "unknown")
        if [[ "$existing_ver" != *"${MBEDTLS_VERSION}"* ]]; then
            log_info "mbedTLS version mismatch ($existing_ver), re-downloading v${MBEDTLS_VERSION}..."
            rm -rf mbedtls mbedtls_build_* mbedtls_install
        fi
    fi

    if [ ! -d "mbedtls" ]; then
        git clone --branch "v${MBEDTLS_VERSION}" --single-branch --depth 1 \
            https://github.com/Mbed-TLS/mbedtls.git mbedtls
    else
        log_info "mbedTLS already exists, skipping download"
    fi

    cd -
}

build_mbedtls() {
    log_info "Building mbedTLS (all ABIs)..."

    TOOLCHAIN_PREFIX="${NDK_PATH}/toolchains/llvm/prebuilt/${HOST_PLATFORM}/bin"
    JOBS="$(nproc 2> /dev/null || sysctl -n hw.ncpu 2> /dev/null || echo 4)"

    ANDROID_ABI_64BIT="$ANDROID_ABI"
    if [[ "$ANDROID_ABI_64BIT" -lt 21 ]]; then
        ANDROID_ABI_64BIT=21
    fi

    build_mbedtls_abi() {
        local abi_name=$1
        local ndk_abi=$2
        local api_level=$3

        local install_dir="$MBEDTLS_INSTALL/$abi_name"
        if [ -d "$install_dir/lib" ] && ls "$install_dir/lib"/*.a &>/dev/null; then
            log_info "  $abi_name: mbedTLS already built, skipping"
            return
        fi

        log_info "  Building mbedTLS $abi_name..."
        local build_dir="$WORK_DIR/mbedtls_build_$abi_name"
        rm -rf "$build_dir"
        mkdir -p "$build_dir"
        cd "$build_dir"

        cmake "$MBEDTLS_DIR" \
            -DCMAKE_TOOLCHAIN_FILE="$NDK_PATH/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI="$ndk_abi" \
            -DANDROID_PLATFORM="android-$api_level" \
            -DCMAKE_INSTALL_PREFIX="$install_dir" \
            -DENABLE_TESTING=OFF \
            -DENABLE_PROGRAMS=OFF \
            -DCMAKE_BUILD_TYPE=Release \
            -DMBEDTLS_FATAL_WARNINGS=OFF \
            -DCMAKE_POLICY_DEFAULT_CMP0057=NEW \
            2>&1 | tail -3

        make -j$JOBS 2>&1 | tail -3
        make install 2>&1 | tail -3
        log_info "  $abi_name: mbedTLS build completed"
        cd -
    }

    build_mbedtls_abi armeabi-v7a armeabi-v7a "$ANDROID_ABI"
    build_mbedtls_abi arm64-v8a arm64-v8a "$ANDROID_ABI_64BIT"
    build_mbedtls_abi x86 x86 "$ANDROID_ABI"
    build_mbedtls_abi x86_64 x86_64 "$ANDROID_ABI_64BIT"

    log_info "All platforms mbedTLS build completed"
}

# ==================== Build FFmpeg ====================

build_ffmpeg() {
    log_info "Starting FFmpeg build (ALAC + HTTP/HTTPS) - all platforms..."
    
    FFMPEG_MODULE_PATH="$WORK_DIR/media/libraries/decoder_ffmpeg/src/main"
    FFMPEG_PATH="$WORK_DIR/ffmpeg"
    
    cd "$FFMPEG_MODULE_PATH/jni"
    
    if [ -L "ffmpeg" ]; then
        rm -f ffmpeg
    fi
    
    ln -s "$FFMPEG_PATH" ffmpeg
    
    cd ffmpeg
    
    JOBS="$(nproc 2> /dev/null || sysctl -n hw.ncpu 2> /dev/null || echo 4)"
    log_info "Using $JOBS concurrent jobs"
    
    COMMON_OPTIONS="
        --target-os=android
        --enable-shared
        --enable-static
        --disable-doc
        --disable-programs
        --disable-everything
        --enable-avcodec
        --enable-avformat
        --enable-swresample
        --enable-avutil
        --enable-decoder=alac
        --enable-decoder=flac
        --enable-decoder=mp3float
        --enable-decoder=aac
        --enable-decoder=vorbis
        --enable-decoder=opus
        --enable-decoder=pcm_s16le
        --enable-decoder=pcm_s24le
        --enable-encoder=alac
        --enable-parser=aac
        --enable-parser=flac
        --enable-parser=mpegaudio
        --enable-parser=vorbis
        --enable-parser=opus
        --enable-demuxer=mov
        --enable-demuxer=mp3
        --enable-demuxer=flac
        --enable-demuxer=ogg
        --enable-demuxer=wav
        --enable-demuxer=aac
        --enable-protocol=file
        --enable-protocol=http
        --enable-protocol=https
        --enable-protocol=tcp
        --enable-protocol=tls
        --enable-mbedtls
        --enable-version3
        --disable-vulkan
    "
    
    TOOLCHAIN_PREFIX="${NDK_PATH}/toolchains/llvm/prebuilt/${HOST_PLATFORM}/bin"
    
    ANDROID_ABI_64BIT="$ANDROID_ABI"
    if [[ "$ANDROID_ABI_64BIT" -lt 21 ]]; then
        ANDROID_ABI_64BIT=21
    fi
    
    build_one_abi() {
        local abi_name=$1
        local arch=$2
        local cpu=$3
        local cross_prefix=$4
        local extra_cflags=$5
        local extra_ldflags=$6
        local extra_opts=$7

        local mbedtls_prefix="$MBEDTLS_INSTALL/$abi_name"
        local tls_cflags="-I${mbedtls_prefix}/include"
        local tls_ldflags="-L${mbedtls_prefix}/lib"

        if [ -n "$extra_cflags" ]; then
            extra_cflags="$extra_cflags $tls_cflags"
        else
            extra_cflags="$tls_cflags"
        fi
        if [ -n "$extra_ldflags" ]; then
            extra_ldflags="$extra_ldflags $tls_ldflags"
        else
            extra_ldflags="$tls_ldflags"
        fi

        log_info "Building FFmpeg $abi_name..."
        make distclean 2>/dev/null || true
        ./configure \
            --libdir=android-libs/$abi_name \
            --incdir=android-libs/include \
            --arch=$arch \
            --cpu=$cpu \
            --cross-prefix="${TOOLCHAIN_PREFIX}/${cross_prefix}" \
            --nm="${TOOLCHAIN_PREFIX}/llvm-nm" \
            --ar="${TOOLCHAIN_PREFIX}/llvm-ar" \
            --ranlib="${TOOLCHAIN_PREFIX}/llvm-ranlib" \
            --strip="${TOOLCHAIN_PREFIX}/llvm-strip" \
            --extra-cflags="$extra_cflags" \
            --extra-ldflags="$extra_ldflags" \
            --extra-libs="-lmbedtls -lmbedx509 -lmbedcrypto" \
            $extra_opts \
            ${COMMON_OPTIONS}
        make -j$JOBS
        make install-libs
        make install-headers
        log_info "$abi_name build completed"
    }

    build_one_abi armeabi-v7a arm armv7-a \
        "armv7a-linux-androideabi${ANDROID_ABI}-" \
        "-march=armv7-a -mfloat-abi=softfp" "-Wl,--fix-cortex-a8" ""

    build_one_abi arm64-v8a aarch64 armv8-a \
        "aarch64-linux-android${ANDROID_ABI_64BIT}-" "" "" ""

    build_one_abi x86 x86 i686 \
        "i686-linux-android${ANDROID_ABI}-" "" "" "--disable-asm"

    build_one_abi x86_64 x86_64 x86-64 \
        "x86_64-linux-android${ANDROID_ABI_64BIT}-" "" "" "--disable-asm"

    log_info "All platforms FFmpeg build completed"

    log_info "Verifying library files..."
    for arch in armeabi-v7a arm64-v8a x86 x86_64; do
        if [ -d "android-libs/$arch" ]; then
            count=$(ls android-libs/$arch/*.so 2>/dev/null | wc -l)
            log_info "  $arch: $count .so files"
        else
            log_error "  $arch: Directory does not exist!"
        fi
    done
    
    cd -
    cd -
}

# ==================== Build AAR ====================

build_aar() {
    log_info "Building lib-decoder-ffmpeg AAR (shared libraries, all platforms)..."

    FFMPEG_PATH="$WORK_DIR/ffmpeg"
    cd "$WORK_DIR/media"

    cd libraries/decoder_ffmpeg/src/main/jni
    cp CMakeLists.txt CMakeLists.txt.bak

    cat > CMakeLists.txt << 'CMAKEOF'
cmake_minimum_required(VERSION 3.21.0 FATAL_ERROR)
set(CMAKE_CXX_STANDARD 11)
project(libffmpegJNI C CXX)

set(ffmpeg_location "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg")
set(ffmpeg_binaries "${ffmpeg_location}/android-libs/${ANDROID_ABI}")
set(ffmpeg_headers "${ffmpeg_location}/android-libs/include")

foreach(ffmpeg_lib avutil swresample avcodec avformat)
    add_library(${ffmpeg_lib} SHARED IMPORTED)
    set_target_properties(${ffmpeg_lib} PROPERTIES
        IMPORTED_LOCATION "${ffmpeg_binaries}/lib${ffmpeg_lib}.so"
    )
endforeach()

include_directories(${ffmpeg_headers})
find_library(android_log_lib log)

add_library(ffmpegJNI SHARED ffmpeg_jni.cc)

target_link_libraries(ffmpegJNI
    PRIVATE android
    PRIVATE avformat swresample avcodec avutil
    PRIVATE ${android_log_lib})

if(ANDROID_ABI STREQUAL "arm64-v8a")
    target_link_options(ffmpegJNI PRIVATE "-Wl,-Bsymbolic")
endif()
CMAKEOF

    cd "$WORK_DIR/media"

    cp common_library_config.gradle common_library_config.gradle.bak

    log_info "Building AAR per ABI..."
    mkdir -p build_temp
    BUILD_DIR="$WORK_DIR/media/build_temp"

    local SAVED_FFMPEG="$WORK_DIR/ffmpeg_saved"
    mkdir -p "$SAVED_FFMPEG"
    for abi in armeabi-v7a arm64-v8a x86 x86_64; do
        mkdir -p "$SAVED_FFMPEG/$abi"
        cp "$FFMPEG_PATH/android-libs/$abi"/lib*.so "$SAVED_FFMPEG/$abi/" 2>/dev/null || true
        cp "$FFMPEG_PATH/android-libs/$abi"/lib*.a "$SAVED_FFMPEG/$abi/" 2>/dev/null || true
    done
    log_info "FFmpeg libraries backed up to $SAVED_FFMPEG"

    for abi in armeabi-v7a arm64-v8a x86 x86_64; do
        log_info "  Building $abi..."

        cp common_library_config.gradle common_library_config.gradle.tmp
        grep -v 'ndk {' common_library_config.gradle.tmp > common_library_config.gradle
        sed "/testInstrumentationRunner/a\\
        ndk {  abiFilters  \"$abi\" }
" common_library_config.gradle > common_library_config.gradle.tmp
        mv common_library_config.gradle.tmp common_library_config.gradle

        ./gradlew lib-decoder-ffmpeg:assembleRelease 2>&1 | tail -5

        if [ -f "libraries/decoder_ffmpeg/buildout/outputs/aar/lib-decoder-ffmpeg-release.aar" ]; then
            cp "libraries/decoder_ffmpeg/buildout/outputs/aar/lib-decoder-ffmpeg-release.aar" \
               "$BUILD_DIR/lib-decoder-ffmpeg-$abi.aar"
            log_info "  $abi AAR saved"
        fi
    done

    cp common_library_config.gradle.bak common_library_config.gradle
    cp libraries/decoder_ffmpeg/src/main/jni/CMakeLists.txt.bak \
       libraries/decoder_ffmpeg/src/main/jni/CMakeLists.txt

    log_info "Merging all ABIs and injecting FFmpeg .so (restoring from backup)..."
    rm -rf "$BUILD_DIR/merge"
    mkdir -p "$BUILD_DIR/merge"
    cd "$BUILD_DIR/merge"

    unzip -qo "$BUILD_DIR/lib-decoder-ffmpeg-arm64-v8a.aar" -x 'jni/*'

    for abi in armeabi-v7a arm64-v8a x86 x86_64; do
        mkdir -p "jni/$abi"
        unzip -jo "$BUILD_DIR/lib-decoder-ffmpeg-$abi.aar" "jni/$abi/*" -d "jni/$abi" 2>/dev/null || true
        cp "$SAVED_FFMPEG/$abi"/lib*.so "jni/$abi/"
        log_info "  $abi: $(ls jni/$abi/*.so 2>/dev/null | wc -l) .so files"
    done

    rm -f "$BUILD_DIR/lib-decoder-ffmpeg-release.aar"
    zip -0 -r "$BUILD_DIR/lib-decoder-ffmpeg-release.aar" . -x ".*" -x "*/.*"

    cd "$WORK_DIR/media"
    log_info "AAR build completed"
}

# ==================== Copy Output ====================

copy_output() {
    log_info "Copying output files..."

    local aar_path="$WORK_DIR/media/build_temp/lib-decoder-ffmpeg-release.aar"

    if [ ! -f "$aar_path" ]; then
        log_error "AAR file not found: $aar_path"
        exit 1
    fi

    mkdir -p app/libs
    cp "$aar_path" app/libs/
    log_info "AAR copied to: app/libs/lib-decoder-ffmpeg-release.aar"

    FFMPEG_PATH="$WORK_DIR/ffmpeg"
    AIRPLAY_FFMPEG="airplay-native/src/main/jniLibs/ffmpeg"

    local SAVED_FFMPEG="$WORK_DIR/ffmpeg_saved"
    log_info "Copying FFmpeg .so/.a and headers to airplay-native..."
    for abi in armeabi-v7a arm64-v8a x86 x86_64; do
        mkdir -p "$AIRPLAY_FFMPEG/$abi"
        cp "$SAVED_FFMPEG/$abi"/lib*.so "$AIRPLAY_FFMPEG/$abi/"
        cp "$SAVED_FFMPEG/$abi"/lib*.a "$AIRPLAY_FFMPEG/$abi/" 2>/dev/null || true
        cp "$MBEDTLS_INSTALL/$abi/lib"/lib*.a "$AIRPLAY_FFMPEG/$abi/" 2>/dev/null || true
    done

    rm -rf "$AIRPLAY_FFMPEG/include"
    mkdir -p "$AIRPLAY_FFMPEG/include"
    cp -r "$FFMPEG_PATH/android-libs/include"/* "$AIRPLAY_FFMPEG/include/"

    log_info "airplay-native FFmpeg files updated"
}

# ==================== Main Flow ====================

main() {
    log_info "====================================="
    log_info "Building lib-decoder-ffmpeg with ALAC support"
    log_info "====================================="
    
    check_dependencies
    download_androidx_media
    download_ffmpeg
    download_mbedtls
    build_mbedtls
    build_ffmpeg
    build_aar
    copy_output
    
    log_info "====================================="
    log_info "Build completed!"
    log_info "====================================="
    log_info ""
    log_info "Output file: app/libs/lib-decoder-ffmpeg-release.aar"
    log_info ""
    log_info "Next steps:"
    log_info "1. Add dependency in app/build.gradle:"
    log_info "   implementation files('libs/lib-decoder-ffmpeg-release.aar')"
    log_info "2. Rebuild project: ./gradlew assembleTempoDebug"
}

main

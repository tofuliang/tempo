#!/bin/bash
set -e
cd "$(dirname "$0")/.."

SETTINGS="settings.gradle"
AAR_OUT="airplay-native/build/outputs/aar/airplay-native-release.aar"
NEED_RESTORE=false

if ! grep -q ':airplay-native' "$SETTINGS"; then
    cp "$SETTINGS" "${SETTINGS}.bak"
    sed 's/include '"'"':app'"'"'/include '"'"':app'"'"', '"'"':airplay-native'"'"'/' "$SETTINGS" > "${SETTINGS}.tmp"
    mv "${SETTINGS}.tmp" "$SETTINGS"
    NEED_RESTORE=true
fi

cleanup() {
    if [ "$NEED_RESTORE" = true ] && [ -f "${SETTINGS}.bak" ]; then
        mv "${SETTINGS}.bak" "$SETTINGS"
    fi
}
trap cleanup EXIT

./gradlew :airplay-native:assembleRelease
cp "$AAR_OUT" libs/
echo "libs/airplay-native-release.aar updated"

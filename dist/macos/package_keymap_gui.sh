#!/bin/sh

set -eu

if [ "$#" -ne 4 ]; then
    echo "usage: $0 INPUT_LEAP_GUI INPUT_LEAPC INPUT_LEAPS OUTPUT_APP" >&2
    exit 2
fi

gui="$1"
client="$2"
server="$3"
app="$4"
contents="$app/Contents"
bundle_id="com.johneybi.input-leap-keymap.client"
version="${INPUTLEAP_KEYMAP_VERSION:-3.0.3}"
minimum_macos="${INPUTLEAP_MINIMUM_MACOS:-11.0}"
macdeployqt="${MACDEPLOYQT:-}"
script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
packaging_revision="$(git -C "$script_dir/../.." rev-parse HEAD)"
python="${PYTHON:-python3}"

verify_build_set() {
    if [ "${KEYSTITCH_ALLOW_DIRTY:-0}" = 1 ]; then
        "$python" "$script_dir/../../tools/release/verify_build.py" "$contents/MacOS" \
            --expected-revision "$packaging_revision" --platform Darwin --allow-dirty "$@"
    else
        "$python" "$script_dir/../../tools/release/verify_build.py" "$contents/MacOS" \
            --expected-revision "$packaging_revision" --platform Darwin "$@"
    fi
}

if [ -z "$macdeployqt" ]; then
    macdeployqt="$(command -v macdeployqt || true)"
fi
if [ -z "$macdeployqt" ]; then
    echo "macdeployqt was not found; set MACDEPLOYQT or add it to PATH" >&2
    exit 1
fi

if [ -e "$app" ] || [ -L "$app" ]; then
    echo "Refusing to replace an existing app; choose a new staging path: $app" >&2
    exit 1
fi
mkdir -p "$contents/MacOS" "$contents/Resources"
cp "$gui" "$contents/MacOS/input-leap"
cp "$client" "$contents/MacOS/input-leapc"
cp "$server" "$contents/MacOS/input-leaps"
chmod 755 "$contents/MacOS/input-leap" \
    "$contents/MacOS/input-leapc" "$contents/MacOS/input-leaps"
verify_build_set
cp "$(dirname "$0")/bundle/InputLeap.app/Contents/Resources/InputLeap.icns" \
    "$contents/Resources/InputLeap.icns"
cp LICENSE "$contents/Resources/LICENSE.txt"

cat > "$contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>English</string>
    <key>CFBundleDisplayName</key>
    <string>Input Leap Keymap</string>
    <key>CFBundleExecutable</key>
    <string>input-leap</string>
    <key>CFBundleIconFile</key>
    <string>InputLeap.icns</string>
    <key>CFBundleIdentifier</key>
    <string>$bundle_id</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>Input Leap Keymap</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>$version</string>
    <key>CFBundleVersion</key>
    <string>$version</string>
    <key>LSMinimumSystemVersion</key>
    <string>$minimum_macos</string>
    <key>KeyStitchPackagingCheckoutRevision</key>
    <string>$packaging_revision</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSLocalNetworkUsageDescription</key>
    <string>Connect to the Input Leap server on your local network.</string>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
</dict>
</plist>
EOF

"$macdeployqt" "$app" -no-strip \
    -executable="$contents/MacOS/input-leapc" \
    -executable="$contents/MacOS/input-leaps"

# Do not sign a package whose declared minimum OS is lower than any bundled
# executable/framework, or which still depends on a build machine path.
"$python" "$script_dir/../../tools/release/audit_macos_bundle.py" "$app"

# Keep Accessibility permission associated with the app across ad-hoc beta
# updates instead of allowing codesign to use the changing binary cdhash.
codesign --force --deep --sign - \
    --identifier "$bundle_id" \
    --requirements "=designated => identifier \"$bundle_id\"" \
    "$app"
codesign --verify --deep --strict --verbose=2 "$app"
codesign -d -r- "$app" 2>&1 | grep -F "identifier \"$bundle_id\"" >/dev/null

# Signing changes Mach-O bytes; record final hashes only after signing.
"$python" "$script_dir/../../tools/release/audit_macos_bundle.py" "$app" \
    --manifest "$app.audit.json"
verify_build_set --output "$app.build.json"

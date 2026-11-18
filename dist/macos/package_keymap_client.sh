#!/bin/sh

set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 INPUT_LEAPC OUTPUT_APP" >&2
    exit 2
fi

binary="$1"
app="$2"
contents="$app/Contents"
version="${INPUTLEAP_KEYMAP_VERSION:-3.0.3}"
bundle_id="com.johneybi.input-leap-keymap.client"
minimum_macos="${INPUTLEAP_MINIMUM_MACOS:-11.0}"
script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
python="${PYTHON:-python3}"

if [ -e "$app" ] || [ -L "$app" ]; then
    echo "Refusing to replace an existing app; choose a new staging path: $app" >&2
    exit 1
fi
mkdir -p "$contents/MacOS" "$contents/Resources"
cp "$binary" "$contents/MacOS/input-leapc"
chmod 755 "$contents/MacOS/input-leapc"
cp "$(dirname "$0")/bundle/InputLeap.app/Contents/Resources/InputLeap.icns" \
    "$contents/Resources/InputLeap.icns"
cp "$script_dir/../../LICENSE" "$contents/Resources/LICENSE.txt"

cat > "$contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDisplayName</key>
    <string>Input Leap Keymap</string>
    <key>CFBundleExecutable</key>
    <string>input-leapc</string>
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
    <key>LSUIElement</key>
    <true/>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSLocalNetworkUsageDescription</key>
    <string>Connect to an Input Leap server on the local network.</string>
</dict>
</plist>
EOF

"$python" "$script_dir/../../tools/release/audit_macos_bundle.py" "$app"

# Keep the designated requirement stable across ad-hoc beta builds. Without an
# explicit requirement, codesign falls back to the changing binary cdhash and
# macOS can invalidate the app's Accessibility permission after an update.
codesign --force --deep --sign - \
    --identifier "$bundle_id" \
    --requirements "=designated => identifier \"$bundle_id\"" \
    "$app"
codesign --verify --deep --strict --verbose=2 "$app"
codesign -d -r- "$app" 2>&1 | grep -F "identifier \"$bundle_id\"" >/dev/null

"$python" "$script_dir/../../tools/release/audit_macos_bundle.py" "$app" \
    --manifest "$app.audit.json"

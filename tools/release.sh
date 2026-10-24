#!/bin/bash
# Builds a brEARk release: universal binaries, unit tests, signed plugins,
# an installer package, notarized and stapled, plus its SHA-256.
#
#   tools/release.sh
#
# Signing and notarization use these environment variables (see docs/DEVELOPMENT.md):
#   BREARK_SIGN_APP        "Developer ID Application: <Name> (<TEAMID>)"
#   BREARK_SIGN_INSTALLER  "Developer ID Installer: <Name> (<TEAMID>)"
#   BREARK_NOTARY_PROFILE  a notarytool keychain profile name
# Without them the package is unsigned, for testing only, and named so.
set -euo pipefail

cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project(brEARk VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
BUILD=build-release
STAGE="$BUILD/stage"
DIST=dist
INSTALLER=tools/installer

SIGN_APP="${BREARK_SIGN_APP:-}"
SIGN_INSTALLER="${BREARK_SIGN_INSTALLER:-}"
NOTARY="${BREARK_NOTARY_PROFILE:-}"

if [[ -n "$SIGN_APP" && -n "$SIGN_INSTALLER" ]]; then
    PKG="$DIST/brEARk-$VERSION-macOS.pkg"
else
    PKG="$DIST/brEARk-$VERSION-macOS-UNSIGNED.pkg"
    echo "No signing identities set: building an UNSIGNED package for testing."
fi

echo "── brEARk $VERSION: build (universal, release) ──"
# BREARK_JUCE_DIR: an existing JUCE 8.0.13 checkout, instead of downloading it again.
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DBREARK_DEVELOPER_BUILD=OFF -DBREARK_COPY_AFTER_BUILD=OFF \
      ${BREARK_JUCE_DIR:+-DFETCHCONTENT_SOURCE_DIR_JUCE="$BREARK_JUCE_DIR"} > "$BUILD.configure.log"
cmake --build "$BUILD" --target brEARk_VST3 brEARk_AU brEARk_Tests

echo "── unit tests ──"
if ! "$BUILD/brEARk_Tests_artefacts/Release/brEARk_Tests" > "$BUILD/tests.log" 2>&1; then
    tail -30 "$BUILD/tests.log"
    exit 1
fi
tail -1 "$BUILD/tests.log"

echo "── stage ──"
rm -rf "$STAGE"
mkdir -p "$STAGE/vst3" "$STAGE/au" "$DIST"
cp -R "$BUILD/brEARk_artefacts/Release/VST3/brEARk.vst3" "$STAGE/vst3/"
cp -R "$BUILD/brEARk_artefacts/Release/AU/brEARk.component" "$STAGE/au/"
VST3="$STAGE/vst3/brEARk.vst3"
AU="$STAGE/au/brEARk.component"

for binary in "$VST3/Contents/MacOS/brEARk" "$AU/Contents/MacOS/brEARk"; do
    archs=$(lipo -archs "$binary")
    [[ "$archs" == *arm64* && "$archs" == *x86_64* ]] || { echo "Not universal ($archs): $binary"; exit 1; }
done

if [[ -n "$SIGN_APP" ]]; then
    echo "── sign plugins ──"
    for bundle in "$VST3" "$AU"; do
        codesign --force --sign "$SIGN_APP" --options runtime --timestamp "$bundle"
        codesign --verify --strict --verbose=2 "$bundle"
    done
fi

echo "── package ──"
# Not relocatable: the installer must put the plugins where we say, even if
# another copy (a developer build) exists elsewhere.
component_package() # <root> <identifier> <install location> <output> [scripts]
{
    local plist="$BUILD/$(basename "$4" .pkg).plist"
    pkgbuild --analyze --root "$1" "$plist" > /dev/null
    plutil -replace 0.BundleIsRelocatable -bool NO "$plist"
    pkgbuild --root "$1" --component-plist "$plist" --identifier "$2" --version "$VERSION" \
             --install-location "$3" ${5:+--scripts "$5"} "$4" > /dev/null
}

component_package "$STAGE/vst3" com.totemphonia.brEARk.vst3 /Library/Audio/Plug-Ins/VST3 "$BUILD/brEARk-vst3.pkg"
component_package "$STAGE/au" com.totemphonia.brEARk.au /Library/Audio/Plug-Ins/Components "$BUILD/brEARk-au.pkg" "$INSTALLER/scripts"

rm -f "$PKG"
productbuild --distribution "$INSTALLER/distribution.xml" --resources "$INSTALLER/resources" --package-path "$BUILD" \
             ${SIGN_INSTALLER:+--sign "$SIGN_INSTALLER" --timestamp} "$PKG" > /dev/null

if [[ -n "$SIGN_INSTALLER" && -n "$NOTARY" ]]; then
    echo "── notarize (a few minutes) ──"
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY" --wait
    xcrun stapler staple "$PKG"
    spctl --assess --type install --verbose=2 "$PKG"
elif [[ -n "$SIGN_INSTALLER" ]]; then
    echo "Signed but NOT notarized (no BREARK_NOTARY_PROFILE): Gatekeeper will still warn."
fi

(cd "$DIST" && shasum -a 256 "$(basename "$PKG")" > "$(basename "$PKG").sha256")
echo "── done ──"
ls -l "$PKG"
cat "$PKG.sha256"

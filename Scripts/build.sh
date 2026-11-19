#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
root="$PWD"
vendor="$root/.build/vendor"
mkdir -p "$vendor"
archive="$vendor/rime.tar.bz2"
digest=11d8dc663c6ec06d5ccb6111ba664a9e7b631b703ac6acd07cffbac664021850
if [[ ! -f "$archive" ]]; then
    curl -fLsS --retry 2 'https://github.com/rime/librime/releases/download/1.17.0/rime-33e7814-macOS-universal.tar.bz2' -o "$archive.download"
    mv "$archive.download" "$archive"
fi
if [[ "$(shasum -a 256 "$archive" | cut -d ' ' -f 1)" != "$digest" ]]; then
    echo "librime 压缩包校验未通过：$archive" >&2
    exit 1
fi
tar -xjf "$archive" -C "$vendor"
work="$(mktemp -d "$root/.build/package.XXXXXX")"
trap 'rm -rf "$work"' EXIT
app="$work/Yuju.app"
contents="$app/Contents"
mkdir -p "$contents"/{MacOS,Frameworks,Resources/Rime/build} "$work/rime-user" "$root/dist"
cp Resources/Info.plist "$contents/"
cp -R Resources/Licenses "$contents/Resources/"
for lang in en zh-Hans; do
    mkdir -p "$contents/Resources/$lang.lproj"
    printf '"local.yuju.inputmethod.Yuju.Hans" = "语句拼音";\n' > "$contents/Resources/$lang.lproj/InfoPlist.strings"
done
swift Scripts/Icon.swift "$contents/Resources/Yuju.pdf"
cp -L "$vendor/dist/lib/librime.1.dylib" "$contents/Frameworks/"
DYLD_LIBRARY_PATH="$vendor/dist/lib" "$vendor/dist/bin/rime_deployer" \
    --build "$work/rime-user" "$root/Resources/Rime" "$contents/Resources/Rime/build" > "$work/rime.log" 2>&1 || { cat "$work/rime.log"; exit 1; }
test -s "$contents/Resources/Rime/build/yuju.table.bin"
for arch in arm64 x86_64; do
    triple="$arch-apple-macos13.0"
    clang -target "$triple" -fobjc-arc -O2 -I"$vendor/dist/include" -c Sources/RimeBridge.m -o "$work/rime-$arch.o"
    swiftc -target "$triple" -swift-version 5 -O -module-name Yuju \
        -import-objc-header Sources/RimeBridge.h Sources/*.swift "$work/rime-$arch.o" \
        -L"$vendor/dist/lib" -lrime -Xlinker -rpath -Xlinker @executable_path/../Frameworks \
        -o "$work/yuju-$arch"
done
lipo -create "$work/yuju-arm64" "$work/yuju-x86_64" -output "$contents/MacOS/Yuju"
codesign -fs - "$contents/Frameworks/librime.1.dylib"
codesign -fs - "$app"
codesign --verify --strict --deep "$app"
ditto "$app" "$root/dist/Yuju.app"
echo "已构建 $root/dist/Yuju.app"

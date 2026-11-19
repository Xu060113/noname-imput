#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
[[ -d dist/Yuju.app ]] || Scripts/build.sh
work="$(mktemp -d "$PWD/.build/checks.XXXXXX")"
trap 'rm -rf "$work"' EXIT
vendor="$PWD/.build/vendor/dist"
clang -fobjc-arc -I"$vendor/include" -c Sources/RimeBridge.m -o "$work/rime.o"
swiftc -parse-as-library -import-objc-header Sources/RimeBridge.h Sources/{DeepSeek,Prediction}.swift \
    Tests/Checks.swift "$work/rime.o" -L"$vendor/lib" -lrime -Xlinker -rpath -Xlinker "$vendor/lib" -o "$work/checks"
"$work/checks" "$PWD/dist/Yuju.app/Contents/Resources/Rime" "$work/user"

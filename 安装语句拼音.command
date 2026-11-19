#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ -d "Yuju.app" ]]; then source_app="$PWD/Yuju.app"
else
    [[ -d dist/Yuju.app ]] || Scripts/build.sh
    source_app="$PWD/dist/Yuju.app"
fi
dest="$HOME/Library/Input Methods/Yuju.app"
mkdir -p "$(dirname "$dest")"
codesign --verify --strict --deep "$source_app"
if [[ -e "$dest" && "$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$dest/Contents/Info.plist")" != "local.yuju.inputmethod.Yuju" ]]; then
    echo "$dest 已有其他应用，请先移走以保留原文件。" >&2
    exit 1
fi
ditto "$source_app" "$dest"
codesign --verify --strict --deep "$dest"
"$dest/Contents/MacOS/Yuju" --register
echo "从菜单栏切换至「语句拼音」，打开「DeepSeek 设置…」即可配置 AI。"

#!/usr/bin/env bash
set -euo pipefail

if [ "$EUID" -ne 0 ]; then
  echo "エラー: root権限で実行してください (例: sudo ./build_and_install.sh)"
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== 1. ローカル環境向けビルド ==="
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
strip --strip-unneeded build/libminimal_addon.so

echo "=== 2. Fcitx5 アドオン配置先の判別 ==="
ADDON_DIR=""
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists fcitx5; then
  LIBDIR="$(pkg-config --variable=libdir fcitx5 || true)"
  [ -n "$LIBDIR" ] && ADDON_DIR="${LIBDIR}/fcitx5"
fi

if [ -z "$ADDON_DIR" ]; then
  for dir in /usr/lib/x86_64-linux-gnu/fcitx5 /usr/lib64/fcitx5 /usr/lib/fcitx5; do
    if [ -d "$dir" ]; then
      ADDON_DIR="$dir"
      break
    fi
  done
fi

if [ -z "$ADDON_DIR" ]; then
  echo "エラー: Fcitx5 のアドオンディレクトリが見つかりません。fcitx5 がインストールされているか確認してください。"
  exit 1
fi

DATA_DIR="/usr/share/fcitx5/minimal_addon"
ADDON_CONF_DIR="/usr/share/fcitx5/addon"
IM_CONF_DIR="/usr/share/fcitx5/inputmethod"

echo "インストール先アドオンディレクトリ: ${ADDON_DIR}"

install -d -m 755 "$ADDON_DIR" "$DATA_DIR" "$ADDON_CONF_DIR" "$IM_CONF_DIR"
install -m 755 "build/libminimal_addon.so" "${ADDON_DIR}/libminimal_addon.so"
install -m 644 "packaging/conf/minimal_addon.conf" "${ADDON_CONF_DIR}/minimal_addon.conf"
install -m 644 "packaging/conf/minimal-ime.conf" "${IM_CONF_DIR}/minimal-ime.conf"

cp -f --remove-destination "data/"* "${DATA_DIR}/"
chmod -R a+rX "$DATA_DIR"

echo "ビルド＆インストール完了！ 一般ユーザー権限で fcitx5 -r -d を実行して Fcitx5 を再起動してください。"

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
strip --strip-unneeded build/libmokz.so

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

DATA_DIR="/usr/share/fcitx5/mokz"
ADDON_CONF_DIR="/usr/share/fcitx5/addon"
IM_CONF_DIR="/usr/share/fcitx5/inputmethod"

echo "インストール先アドオンディレクトリ: ${ADDON_DIR}"

install -d -m 755 "$ADDON_DIR" "$DATA_DIR" "$ADDON_CONF_DIR" "$IM_CONF_DIR"
install -m 755 "build/libmokz.so" "${ADDON_DIR}/libmokz.so"
install -m 644 "packaging/conf/addon/mokz.conf" "${ADDON_CONF_DIR}/mokz.conf"
install -m 644 "packaging/conf/inputmethod/mokz.conf" "${IM_CONF_DIR}/mokz.conf"

cp -f --remove-destination "data/"* "${DATA_DIR}/"
chmod -R a+rX "$DATA_DIR"

echo "ビルド＆インストール完了！ 一般ユーザー権限で fcitx5 -r -d  # X11環境のみ。Wayland環境(KDE Plasma等)はログアウト→再ログインで反映 を実行して Fcitx5 を再起動してください。"

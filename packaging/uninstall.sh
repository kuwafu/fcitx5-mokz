#!/usr/bin/env bash
set -euo pipefail

if [ "$EUID" -ne 0 ]; then
  echo "エラー: root権限で実行してください (例: sudo ./uninstall.sh)"
  exit 1
fi

for dir in /usr/lib/x86_64-linux-gnu/fcitx5 /usr/lib64/fcitx5 /usr/lib/fcitx5; do
  rm -f "${dir}/libminimal_addon.so"
done

rm -f /usr/share/fcitx5/addon/minimal_addon.conf
rm -f /usr/share/fcitx5/inputmethod/minimal-ime.conf
rm -rf /usr/share/fcitx5/minimal_addon

echo "アンインストール完了（ユーザー学習データは ~/.local/share/fcitx5/minimal_addon に残っています）。"

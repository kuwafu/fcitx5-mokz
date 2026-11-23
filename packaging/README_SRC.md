# Minimal IME (minimal_addon) for Fcitx5 — ソースコード配布版

お使いの環境（Ubuntu 22.04 / Debian 12 / Fedora / Arch Linux 等）のコンパイラ・glibc に合わせてローカルビルド＆インストールするためのソース配布パッケージです。
marisa-trie のソースおよび辞書・モデルバイナリ（data/）を同梱しているため、追加のライブラリビルドなしで導入できます。

## 1. ビルドに必要なパッケージの導入

- Debian / Ubuntu 系:

    sudo apt update && sudo apt install -y build-essential cmake pkg-config libfcitx5core-dev

- Fedora / RHEL 系:

    sudo dnf install -y gcc-c++ cmake pkgconf-pkg-config fcitx5-devel

- Arch Linux 系:

    sudo pacman -S --needed base-devel cmake pkgconf fcitx5

## 2. ビルド ＆ インストール

    tar -xzf minimal_addon-src.tar.gz
    cd minimal_addon-src
    sudo ./build_and_install.sh
    fcitx5 -r -d

再起動後、Fcitx5 設定 (fcitx5-configtool) を開き、入力メソッド一覧から「Minimal IME」を追加してください。

## 3. アンインストール

    sudo ./uninstall.sh
    fcitx5 -r -d

※ユーザー学習データは ~/.local/share/fcitx5/minimal_addon に保持されます。

## 反映方法の注意（Wayland / X11）
- **Wayland 環境（KDE Plasma 6 / GNOME 等）**: `fcitx5 -r -d` を手動実行するとコンポジタとの接続が切れる場合があります。インストール後は **ログアウト → 再ログイン**（またはシステム設定の仮想キーボード再適用）で反映してください。
- **X11 環境**: `fcitx5 -r -d` で再起動して反映できます。

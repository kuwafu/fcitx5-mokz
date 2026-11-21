# Minimal IME (minimal_addon) for Fcitx5 (x86_64)

高速Viterbi探索・MARISA-trie辞書・ニューラルリランカーを搭載した Fcitx5 用日本語入力アドオンです。

## 対応環境
- アーキテクチャ: x86_64 (AVX2 / AVX-512 ランタイム自動最適化・非対応CPU自動フォールバック)
- 公式サポート: Wayland 環境 (KDE Plasma 6 / GNOME / Hyprland 等)
- ベストエフォート: X11 環境 (Linux Mint / Xfce / i3wm 等 ※事前に im-config 等で Fcitx5 の有効化が必要)
- 対応ディストロ: Debian / Ubuntu 系、Fedora / RHEL 系、Arch Linux 系

## インストール方法

    tar -xzf minimal_addon-x86_64.tar.gz
    cd minimal_addon-x86_64
    sudo ./install.sh
    fcitx5 -r -d

再起動後、Fcitx5 設定 (fcitx5-configtool) を開き、入力メソッド一覧から「Minimal IME」を追加してください。

## 主な操作仕様
- Space / Tab: 変換候補の展開・選択移動
- Enter: 確定
- Shift + 英字: ローマ字変換をバイパスして半角大文字を直接入力
- 変換中の BackSpace: 1文字ずつ削って再変換（候補ウィンドウ維持）

## アンインストール方法

    sudo ./uninstall.sh
    fcitx5 -r -d

※ユーザー学習データは ~/.local/share/fcitx5/minimal_addon に保持されます。不要な場合は手動で削除してください。

# fcitx5-minimal-addon (Minimal IME)

高速 Viterbi 探索・MARISA-trie 辞書（約160万語）・4-gram KenLM・ニューラルリランカーを搭載した C++20 製 Fcitx5 日本語入力アドオンです。

## 配布パッケージ（Releases）
- **minimal_addon-x86_64.tar.gz**: x86_64 向けコンパイル済みバイナリ ＋ 辞書・モデルバイナリ同梱版
- **minimal_addon-src.tar.gz**: ローカル環境向けソースコード ＋ marisa-trie ソース ＋ 辞書・モデルバイナリ同梱版

## ビルド ＆ パッケージ生成

    ./package.sh

dist/ 配下にコンパイル版・ソースコード版の両アーカイブが生成されます。

## インストール
- コンパイル版を使う場合: アーカイブ展開後、`sudo ./install.sh`
- ソースからビルドする場合: アーカイブ展開後、`sudo ./build_and_install.sh`

詳細は各パッケージ内の README.md を参照してください。

## 反映方法の注意（Wayland / X11）
- **Wayland 環境（KDE Plasma 6 / GNOME 等）**: `fcitx5 -r -d` を手動実行するとコンポジタとの接続が切れる場合があります。インストール後は **ログアウト → 再ログイン**（またはシステム設定の仮想キーボード再適用）で反映してください。
- **X11 環境**: `fcitx5 -r -d` で再起動して反映できます。

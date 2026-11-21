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

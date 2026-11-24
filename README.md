# fcitx5-mokz

Fcitx5 で使える日本語 IME です。

## インストール (x86_64)

[Releases](https://github.com/kuwafu/fcitx5-mokz/releases) から `fcitx5-mokz-v1.0.1-x86_64.tar.gz` を落として実行します。

```bash
F=$(find . ~/Downloads ~/ダウンロード -maxdepth 1 -name "fcitx5-mokz-v1.0.1-x86_64.tar.gz" 2>/dev/null | head -n1) && \
tar -xzf "$F" && \
cd fcitx5-mokz-v1.0.1-x86_64 && \
sudo ./install.sh && \
fcitx5 -r -d
```

Fcitx5 の設定画面から **Mokz** を追加すれば使えます。

## ビルド

```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j$(nproc)
sudo cmake --install build
```

## ライセンス

`LICENSE` および `THIRD_PARTY_LICENSES.md` を参照してください。

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

## ディストロ別 依存パッケージ

### 依存パッケージ一覧

| ディストリビューション | 実行時依存パッケージ | ソースビルド用依存パッケージ |
| --- | --- | --- |
| **Fedora** | `fcitx5` (`fcitx5-configtool`) | `gcc-c++`, `cmake`, `make`, `pkgconf-pkg-config`, `fcitx5-devel` |
| **Ubuntu / Debian** | `fcitx5` (`fcitx5-config-qt`) | `build-essential`, `cmake`, `pkg-config`, `libfcitx5core-dev` |
| **Arch Linux** | `fcitx5` (`fcitx5-configtool`) | `base-devel`, `cmake`, `pkgconf`, `fcitx5` |

### 依存パッケージ導入コマンド

#### Fedora

```bash
# 実行時依存
sudo dnf install fcitx5 fcitx5-configtool

# ソースビルド用依存
sudo dnf install gcc-c++ cmake make pkgconf-pkg-config fcitx5-devel
```

#### Ubuntu / Debian

```bash
# 実行時依存
sudo apt update && sudo apt install fcitx5 fcitx5-config-qt

# ソースビルド用依存
sudo apt update && sudo apt install build-essential cmake pkg-config libfcitx5core-dev
```

#### Arch Linux

```bash
# 実行時依存
sudo pacman -S fcitx5 fcitx5-configtool

# ソースビルド用依存
sudo pacman -S base-devel cmake pkgconf fcitx5
```

## ディストロ別 パッケージ利用手順

### Arch Linux (`PKGBUILD`)

`packaging/arch/PKGBUILD` (`fcitx5-mokz-bin`) を使用して、GitHub Releases のビルド済みアーカイブからパッケージを作成・インストールできます（`/usr/lib/fcitx5/` および `/usr/share/fcitx5/` に配置されます）。

```bash
cd packaging/arch
makepkg -si
```

### Debian / Ubuntu (`.deb` パッケージ)

`packaging/deb/build-deb.sh` を使用して、v1.0.1 のリリースアーカイブ（またはローカルのビルド成果物）から `.deb` パッケージを生成できます（`/usr/lib/x86_64-linux-gnu/fcitx5/` および `/usr/share/fcitx5/` に配置されます）。

```bash
# リリースアーカイブから .deb を生成してインストール
./packaging/deb/build-deb.sh
sudo apt install ./fcitx5-mokz_1.0.1_amd64.deb
```

手元のリリースアーカイブやローカルビルド成果物を使う場合は以下のようにオプションを指定できます。

```bash
# ダウンロード済みアーカイブを指定する場合
./packaging/deb/build-deb.sh --archive /path/to/fcitx5-mokz-v1.0.1-x86_64.tar.gz

# ローカルビルド成果物から生成する場合
./packaging/deb/build-deb.sh --from-build --build-dir ./build --data-dir /usr/share/fcitx5/mokz
```

### Fedora

[Releases](https://github.com/kuwafu/fcitx5-mokz/releases) の `fcitx5-mokz-v1.0.1-x86_64.tar.gz` を展開し、`sudo ./install.sh` を実行すると `/usr/lib64/fcitx5/` を自動判別して配置されます。ソースからビルドしてインストールする場合は、以下のようにアドオン配置先を指定してください。

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DFCITX5_INSTALL_ADDON_DIR=lib64/fcitx5
cmake --build build -j$(nproc)
sudo cmake --install build
```

> **Note:** インストール後の反映は X11 環境では `fcitx5 -r -d`、Wayland 環境（KDE Plasma / GNOME 等）ではログアウト後の再ログインを推奨します。

## ライセンス

`LICENSE` および `THIRD_PARTY_LICENSES.md` を参照してください。

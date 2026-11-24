#!/usr/bin/env bash
set -euo pipefail

BIN_NAME="fcitx5-mokz-v1.0.1-x86_64"
SRC_NAME="fcitx5-mokz-v1.0.1-source"
BIN_DIR="dist/${BIN_NAME}"
SRC_DIR="dist/${SRC_NAME}"

echo "=== 1. クリーンビルド ==="
rm -rf build dist
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

echo "=== 2. バイナリ検証（依存ライブラリ & ハードコードパス） ==="
ldd build/libmokz.so
if ldd build/libmokz.so | grep -q "libmarisa"; then
  echo "エラー: libmarisa が動的リンクのままです"
  exit 1
fi
if strings build/libmokz.so | grep -E "/home/|/workspace/"; then
  echo "エラー: バイナリ内にローカル開発パスが残存しています"
  exit 1
fi

echo "=== 3. バイナリ配布アーカイブ構成 (${BIN_NAME}.tar.gz) ==="
mkdir -p "${BIN_DIR}/conf" "${BIN_DIR}/data"
cp build/libmokz.so "${BIN_DIR}/"
strip --strip-unneeded "${BIN_DIR}/libmokz.so"
cp -r packaging/conf/* "${BIN_DIR}/conf/"
cp packaging/install.sh packaging/uninstall.sh packaging/README.md "${BIN_DIR}/"
[ -f LICENSE ] && cp LICENSE "${BIN_DIR}/"
[ -f THIRD_PARTY_LICENSES.md ] && cp THIRD_PARTY_LICENSES.md "${BIN_DIR}/"

for f in dict.bin kenlm.binary reranker.bin connection.bin; do
  cp -L "/usr/share/fcitx5/mokz/${f}" "${BIN_DIR}/data/${f}"
done

tar -czf "dist/${BIN_NAME}.tar.gz" -C dist "${BIN_NAME}"

echo "=== 4. ソースコード配布アーカイブ構成 (${SRC_NAME}.tar.gz) ==="
mkdir -p "${SRC_DIR}/packaging/conf" "${SRC_DIR}/data"
cp CMakeLists.txt "${SRC_DIR}/"
cp -r src "${SRC_DIR}/"
rm -rf "${SRC_DIR}/src/_backup_"*
cp -r third_party "${SRC_DIR}/"
cp -r packaging/conf/* "${SRC_DIR}/packaging/conf/"
cp packaging/build_and_install.sh "${SRC_DIR}/"
cp packaging/uninstall.sh "${SRC_DIR}/"
cp packaging/README_SRC.md "${SRC_DIR}/README.md"
[ -f LICENSE ] && cp LICENSE "${SRC_DIR}/"
[ -f THIRD_PARTY_LICENSES.md ] && cp THIRD_PARTY_LICENSES.md "${SRC_DIR}/"

if [ -d dict_tools ]; then
  mkdir -p "${SRC_DIR}/dict_tools"
  find dict_tools -maxdepth 2 -type f \( -name "*.py" -o -name "*.sh" -o -name "*.cpp" -o -name "*.hpp" -o -name "*.md" \) -exec cp {} "${SRC_DIR}/dict_tools/" \;
fi

for f in dict.bin kenlm.binary reranker.bin connection.bin; do
  cp -L "/usr/share/fcitx5/mokz/${f}" "${SRC_DIR}/data/${f}"
done

tar -czf "dist/${SRC_NAME}.tar.gz" -C dist "${SRC_NAME}"

echo "=== 完了: dist/ 配下にバイナリ版・ソース版の両方を生成しました ==="
ls -lh dist/*.tar.gz

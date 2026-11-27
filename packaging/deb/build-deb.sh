#!/usr/bin/env bash
set -euo pipefail

PKG_NAME="fcitx5-mokz"
VERSION="1.0.1"
ARCH="amd64"
RELEASE_URL="https://github.com/kuwafu/fcitx5-mokz/releases/download/v${VERSION}/fcitx5-mokz-v${VERSION}-x86_64.tar.gz"
RELEASE_SHA256="b29fc0959126e2d9a16c435a9a8e5798058eaaf6086925ad3948cbd04d43dc3c"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

ARCHIVE_PATH=""
FROM_BUILD=false
BUILD_DIR="${REPO_ROOT}/build"
DATA_DIR=""
OUTPUT_PATH=""

usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Build a Debian/Ubuntu .deb package for ${PKG_NAME} (${VERSION}, ${ARCH}).
Installs libmokz.so into /usr/lib/x86_64-linux-gnu/fcitx5/ and assets into /usr/share/fcitx5/.

Options:
  -a, --archive <path>     Use an existing release tarball (fcitx5-mokz-v${VERSION}-x86_64.tar.gz)
      --from-build         Build package from local build artifacts instead of release archive
      --build-dir <dir>    Build directory containing libmokz.so (default: ${BUILD_DIR})
      --data-dir <dir>     Directory containing dict.bin/kenlm.binary/reranker.bin/connection.bin
                           (used with --from-build; default: /usr/share/fcitx5/mokz)
  -o, --output <path>      Output .deb file path (default: ./${PKG_NAME}_${VERSION}_${ARCH}.deb)
  -h, --help               Show this help message
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    -a|--archive)
      ARCHIVE_PATH="${2:?Missing argument for $1}"
      shift 2
      ;;
    --from-build)
      FROM_BUILD=true
      shift
      ;;
    --build-dir)
      BUILD_DIR="${2:?Missing argument for $1}"
      shift 2
      ;;
    --data-dir)
      DATA_DIR="${2:?Missing argument for $1}"
      shift 2
      ;;
    -o|--output)
      OUTPUT_PATH="${2:?Missing argument for $1}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "エラー: 不明なオプション: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if ! command -v dpkg-deb >/dev/null 2>&1; then
  echo "エラー: dpkg-deb が見つかりません。dpkg パッケージをインストールしてください。" >&2
  exit 1
fi

if [ -z "$OUTPUT_PATH" ]; then
  OUTPUT_PATH="$(pwd)/${PKG_NAME}_${VERSION}_${ARCH}.deb"
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

PKG_ROOT="${WORK_DIR}/${PKG_NAME}_${VERSION}_${ARCH}"
install -d -m 755 \
  "${PKG_ROOT}/DEBIAN" \
  "${PKG_ROOT}/usr/lib/x86_64-linux-gnu/fcitx5" \
  "${PKG_ROOT}/usr/share/fcitx5/addon" \
  "${PKG_ROOT}/usr/share/fcitx5/inputmethod" \
  "${PKG_ROOT}/usr/share/fcitx5/mokz" \
  "${PKG_ROOT}/usr/share/doc/${PKG_NAME}"

if [ "$FROM_BUILD" = true ]; then
  SO_FILE="${BUILD_DIR}/libmokz.so"
  if [ ! -f "$SO_FILE" ]; then
    echo "エラー: ${SO_FILE} が見つかりません。先にビルドを実行してください。" >&2
    exit 1
  fi

  if [ -z "$DATA_DIR" ]; then
    if [ -d "${REPO_ROOT}/data" ]; then
      DATA_DIR="${REPO_ROOT}/data"
    else
      DATA_DIR="/usr/share/fcitx5/mokz"
    fi
  fi

  install -m 755 "$SO_FILE" "${PKG_ROOT}/usr/lib/x86_64-linux-gnu/fcitx5/libmokz.so"
  install -m 644 "${REPO_ROOT}/packaging/conf/addon/mokz.conf" \
    "${PKG_ROOT}/usr/share/fcitx5/addon/mokz.conf"
  install -m 644 "${REPO_ROOT}/packaging/conf/inputmethod/mokz.conf" \
    "${PKG_ROOT}/usr/share/fcitx5/inputmethod/mokz.conf"

  for f in dict.bin kenlm.binary reranker.bin connection.bin; do
    if [ ! -f "${DATA_DIR}/${f}" ]; then
      echo "エラー: データファイル ${DATA_DIR}/${f} が見つかりません。" >&2
      exit 1
    fi
    install -m 644 "${DATA_DIR}/${f}" "${PKG_ROOT}/usr/share/fcitx5/mokz/${f}"
  done

  [ -f "${REPO_ROOT}/LICENSE" ] && install -m 644 "${REPO_ROOT}/LICENSE" \
    "${PKG_ROOT}/usr/share/doc/${PKG_NAME}/copyright"
  [ -f "${REPO_ROOT}/THIRD_PARTY_LICENSES.md" ] && install -m 644 "${REPO_ROOT}/THIRD_PARTY_LICENSES.md" \
    "${PKG_ROOT}/usr/share/doc/${PKG_NAME}/THIRD_PARTY_LICENSES.md"
else
  if [ -z "$ARCHIVE_PATH" ]; then
    for candidate in \
      "./fcitx5-mokz-v${VERSION}-x86_64.tar.gz" \
      "${REPO_ROOT}/dist/fcitx5-mokz-v${VERSION}-x86_64.tar.gz"; do
      if [ -f "$candidate" ]; then
        ARCHIVE_PATH="$candidate"
        break
      fi
    done
  fi

  if [ -z "$ARCHIVE_PATH" ]; then
    if ! command -v curl >/dev/null 2>&1; then
      echo "エラー: curl が見つかりません。--archive でローカルのアーカイブを指定するか curl をインストールしてください。" >&2
      exit 1
    fi
    ARCHIVE_PATH="${WORK_DIR}/fcitx5-mokz-v${VERSION}-x86_64.tar.gz"
    echo "リリースアーカイブを取得しています: ${RELEASE_URL}"
    curl -fsSL -o "$ARCHIVE_PATH" "$RELEASE_URL"
    if command -v sha256sum >/dev/null 2>&1; then
      echo "${RELEASE_SHA256}  ${ARCHIVE_PATH}" | sha256sum -c -
    fi
  fi

  if [ ! -f "$ARCHIVE_PATH" ]; then
    echo "エラー: アーカイブが見つかりません: ${ARCHIVE_PATH}" >&2
    exit 1
  fi

  EXTRACT_DIR="${WORK_DIR}/extract"
  mkdir -p "$EXTRACT_DIR"
  tar -xzf "$ARCHIVE_PATH" -C "$EXTRACT_DIR"

  SRC_DIR="${EXTRACT_DIR}/fcitx5-mokz-v${VERSION}-x86_64"
  if [ ! -d "$SRC_DIR" ]; then
    SRC_DIR="$(find "$EXTRACT_DIR" -mindepth 1 -maxdepth 1 -type d | head -n1)"
  fi

  if [ -z "$SRC_DIR" ] || [ ! -f "${SRC_DIR}/libmokz.so" ]; then
    echo "エラー: アーカイブ内に libmokz.so が見つかりません。" >&2
    exit 1
  fi

  install -m 755 "${SRC_DIR}/libmokz.so" \
    "${PKG_ROOT}/usr/lib/x86_64-linux-gnu/fcitx5/libmokz.so"
  install -m 644 "${SRC_DIR}/conf/addon/mokz.conf" \
    "${PKG_ROOT}/usr/share/fcitx5/addon/mokz.conf"
  install -m 644 "${SRC_DIR}/conf/inputmethod/mokz.conf" \
    "${PKG_ROOT}/usr/share/fcitx5/inputmethod/mokz.conf"
  install -m 644 "${SRC_DIR}/data/"* \
    "${PKG_ROOT}/usr/share/fcitx5/mokz/"

  [ -f "${SRC_DIR}/LICENSE" ] && install -m 644 "${SRC_DIR}/LICENSE" \
    "${PKG_ROOT}/usr/share/doc/${PKG_NAME}/copyright"
  [ -f "${SRC_DIR}/THIRD_PARTY_LICENSES.md" ] && install -m 644 "${SRC_DIR}/THIRD_PARTY_LICENSES.md" \
    "${PKG_ROOT}/usr/share/doc/${PKG_NAME}/THIRD_PARTY_LICENSES.md"
fi

INSTALLED_SIZE="$(du -sk "${PKG_ROOT}/usr" | awk '{print $1}')"

cat > "${PKG_ROOT}/DEBIAN/control" <<EOF
Package: ${PKG_NAME}
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Maintainer: Kuwafu <https://github.com/kuwafu/fcitx5-mokz>
Homepage: https://github.com/kuwafu/fcitx5-mokz
Depends: fcitx5, libc6 (>= 2.33)
Installed-Size: ${INSTALLED_SIZE}
Description: Japanese IME addon for Fcitx5
 Mokz is a Japanese input method engine for Fcitx5 featuring fast Viterbi
 search, a statically linked MARISA-trie dictionary, and a neural reranker.
EOF
chmod 644 "${PKG_ROOT}/DEBIAN/control"

if command -v md5sum >/dev/null 2>&1; then
  (cd "${PKG_ROOT}" && find usr -type f -print0 | sort -z | xargs -0 md5sum > DEBIAN/md5sums)
  chmod 644 "${PKG_ROOT}/DEBIAN/md5sums"
fi

mkdir -p "$(dirname "$OUTPUT_PATH")"
if dpkg-deb --help 2>&1 | grep -q -- '--root-owner-group'; then
  dpkg-deb --build --root-owner-group "${PKG_ROOT}" "${OUTPUT_PATH}"
else
  dpkg-deb --build "${PKG_ROOT}" "${OUTPUT_PATH}"
fi

echo "ビルド完了: ${OUTPUT_PATH}"

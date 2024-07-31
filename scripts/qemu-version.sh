#!/bin/sh

set -eu

dir="$1"
pkgversion="$2"
version="$3"
hash=""

if [ -z "$pkgversion" ]; then
    cd "$dir"
    if [ -e .git ]; then
        pkgversion=$(git describe --match 'v*' --dirty) || :
    fi
fi

if [ -n "$pkgversion" ]; then
    fullversion="$version ($pkgversion)"
else
    fullversion="$version"
fi

cd "$dir"
if [ -e .git ]; then
    hash="$(git rev-parse --verify HEAD | sed -E 's/[[:xdigit:]]{2}/0x&, /g; s/, $//')" || :
fi

cat <<EOF
#define QEMU_PKGVERSION "$pkgversion"
#define QEMU_FULL_VERSION "$fullversion"
#define QEMU_GIT_HASH { $hash }
EOF

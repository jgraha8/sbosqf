#!/bin/bash

if (( $# != 1 )); then
    echo "Usage: `basename $0` pkg"
    exit 1
fi

if [[ ! -f PKGLIST ]]; then
    echo "PKGLIST not found"
    exit 1
fi

pkg=$1

chk=$(grep -x $pkg PKGLIST)

if [[ -z $chk ]]; then
    echo "$pkg not in PKGLIST"
    exit 1
fi

set -e

git rm -f $pkg
sed -i "/^${pkg}/d" PKGLIST
sed -i "/^${pkg}/d" REVIEWED
echo "removed $pkg"



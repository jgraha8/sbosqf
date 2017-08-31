#!/bin/bash

if (( $# == 0 )); then
    echo "Usage: `basename $0` pkg"
    exit 1
fi

if [[ ! -f PKGLIST ]]; then
    echo "PKGLIST not found"
    exit 1
fi

for pkg in $@
do

    chk=$(grep -x $pkg PKGLIST)

    if [[ -z $chk ]]; then
	echo "$pkg not in PKGLIST"
	#exit 1
	continue
    fi

    mkdir -p archive &&
    sed -i "/^${pkg}/d" PKGLIST &&
    sed -i "/^${pkg}/d" REVIEWED
    [ -f $pkg ] && git mv $pkg archive/$pkg
    echo "removed $pkg"
done


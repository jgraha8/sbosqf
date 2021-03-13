#!/bin/bash

if (( $# == 0 )); then
    echo "Usage: `basename $0` pkg"
    exit 1
fi

if [[ ! -f PKGDB ]]; then
    echo "PKGDB not found"
    exit 1
fi

mkdir -p archive
for pkg in $@
do

    if [[ -z $(grep -x $pkg PKGDB) ]]; then
	echo "$pkg not in PKGDB"
    else
	sed -i "/^${pkg}/d" PKGDB
    fi
    
    # if [[ -z $(grep -x $pkg REVIEWED) ]]; then
    # 	echo "$pkg not in REVIEWED"
    # else
    # 	sed -i "/^${pkg}/d" REVIEWED
    # fi
    [ -f $pkg ] && git mv $pkg archive/$pkg
    rm -f $pkg # In case it's not tracked in git
    
    echo "removed $pkg"
done


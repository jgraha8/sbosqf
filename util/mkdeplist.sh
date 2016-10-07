#!/bin/bash

DEPLIST=../deplist

rm -f $DEPLIST
( 
    cd deps

    while read d
    do
	echo $d
	echo -n "${d}: " >> $DEPLIST 
	list=( `./sbopkg-dep2sqf -o -l $d` )

	N=${#list[@]}
	pkg=${list[$N-1]} # Package is at the end of the list
	
	if [[ $pkg != $d ]]; then
	    echo "expected package name does not match the dep file name" 
	    exit 1
	fi

	for (( n=0; n<N-1; ++n ))
	do
	    echo -n "${list[$n]} " >> $DEPLIST
	done
	echo >> $DEPLIST
    done<PKGLIST
)


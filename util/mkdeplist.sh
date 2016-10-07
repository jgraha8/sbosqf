#!/bin/bash

DEPLIST=../deplist

( 
    cd deps

    rm -f $DEPLIST

    while read d
    do
	echo -n "$d"
	echo -n "${d}: " >> $DEPLIST 
	list=( `./sbopkg-dep2sqf -o -l $d` )

	pkg=""
	N=${#list[@]}
	if (( N == 0 )); then
	    echo ": empty dep list for $d--possibly an empty METAPKG"
	    echo >> $DEPLIST
	    continue
	else
	    pkg=${list[$N-1]} # Package is at the end of the list
	fi

	# Checking if the expected package matches
	if [[ $pkg != $d ]]; then
	    echo -n ": expected package name does not match the dep file name--assuming a METAPKG" 
	fi

	echo -n ": "
	for (( n=0; n<N-1; ++n ))
	do
	    echo -n "${list[$n]} "
	    echo -n "${list[$n]} " >> $DEPLIST
	done
	echo >> $DEPLIST

	echo
    done<PKGLIST
)


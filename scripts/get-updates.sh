#!/bin/bash

rm -f u
while read d
do
    t=$(echo $d | cut -d'|' -f1)
    t=$(echo $t)
    ok=$(grep -x "$t" updates 2>/dev/null)
    echo "$d : $t : $ok"
    [[ ! -z $ok ]] && echo $d >> u
done<updates.sqf

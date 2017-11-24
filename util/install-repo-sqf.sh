#!/bin/bash

set -e

source ~/.slack-repo
DEP_DIR=~/slackware/sbopkg-dep2sqf/deps

while read p
do
    [ ! -d $REPO_DIR/$p ] && continue

    sbopkg-dep2sqf -o $p
    
    rm -f $REPO_DIR/$p/$p.sqf
    mv $p.sqf $REPO_DIR/$p/$p.sqf

done<${DEP_DIR}/PKGLIST


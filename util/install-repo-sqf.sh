#!/bin/bash

set -e

source ~/.slack-repo
DEP_DIR=~/slackware/sbopkg-dep2sqf/deps
#QUEUE_DIR=$REPO_DIR/SQF

#rm -rf $QUEUE_DIR
#mkdir -p $QUEUE_DIR

cd $QUEUE_DIR

while read p
do
    [ ! -d $REPO_DIR/$p ] && continue

    sbopkg-dep2sqf -o $p
    
    rm -f $REPO_DIR/$p/$p.sqf
    mv $p.sqf $REPO_DIR/$p/$p.sqf

done<${DEP_DIR}/PKGLIST


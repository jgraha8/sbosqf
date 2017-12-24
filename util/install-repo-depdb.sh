#!/bin/bash

set -e

source ~/.slack-repo
DEP_DIR=~/slackware/sbopkg-dep2sqf/deps

sbopkg-dep2sqf -o -d -f

echo -n > $REPO_DIR/DEPDB
while read line
do
    p=$(echo $line | cut -d: -f1)

    [ ! -d $REPO_DIR/$p ] && continue

    echo $line >> $REPO_DIR/DEPDB

done<${DEP_DIR}/DEPDB


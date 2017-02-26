#!/bin/bash

set -e

TMP_FILE=.sbopkg-updates
UPDATES_FILE=sbopkg-updates

trap 'rm -f $UPDATES_FILE $TMP_FILE ; exit 1' TERM INT

sbopkg -c 2> /dev/null | grep ^[a-z] | grep : | cut -d: -f1 > $TMP_FILE

cat > $UPDATES_FILE<<EOF
METAPKG
REQUIRED:
EOF
cat $TMP_FILE >> $UPDATES_FILE

rm -f $TMP_FILE

echo "Created sbopkg-updates METAPKG file"

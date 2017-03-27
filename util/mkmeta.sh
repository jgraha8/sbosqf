#!/bin/bash

if (( $# == 0 )); then
    echo "Usage $(basename $0) dep-files..."
    exit 0
fi

cat > meta <<EOF
METAPKG
REQUIRED:
EOF

for pkg in $@
do
    echo $pkg >> meta
done

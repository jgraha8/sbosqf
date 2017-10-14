#!/bin/bash

set -e

pkg=""
for p in /var/log/packages/*_SBo
do
    pkg="$pkg $(slack-parsepkg.sh $p | cut -d: -f1)"
done
echo $pkg

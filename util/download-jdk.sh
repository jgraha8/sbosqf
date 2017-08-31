#!/bin/bash

url=$(sbopkg-dep2sqf -R jdk | grep DOWNLOAD_x86_64 | cut -d= -f2 | sed 's/\"//g')
tgz=${url##*/}

sudo wget -O /var/cache/sbopkg/$tgz \
     -c --header "Cookie: oraclelicense=accept-securebackup-cookie" \
     $url


#!/bin/bash

rm -rf nanogi-fs
mkdir nanogi-fs
tar -C nanogi-fs -jxf ../nanogi-dist.tar.bz2

node setup.js input.config.json input.runtime.json $(id -u) $(id -g)

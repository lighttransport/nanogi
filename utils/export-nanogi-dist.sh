#!/bin/bash

sudo docker export $(sudo docker create nanogi-dist /bin/ls) > nanogi-dist.tar
bzip2 -f nanogi-dist.tar

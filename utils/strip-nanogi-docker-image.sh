#!/bin/bash

# strip-docker-image can be obtained here: https://github.com/mvanholsteijn/strip-docker-image

# stripped-nanogi will be less than ~30MB in its container size.

sudo strip-docker-image -i nanogi -t nanogi-dist  -v \
                           -f /nanogi/build/bin/nanogi \
                           -f /etc/passwd \
                           -f /etc/group \
                           -f '/usr/local/lib/*' \
                           -f '/lib/*/libnss*' \
                           -f /bin/ls \
                           -f /bin/cat \
                           -f /bin/bash \
                           -f /bin/sh \
                           -f /bin/mkdir \
                           -f /bin/ps \
                           -f /var/run 

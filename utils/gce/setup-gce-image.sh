#/bin/bash -x

apt-get install -y build-essential
apt-get install -y libprotobuf-c1 libprotobuf-dev protobuf-c-compiler protobuf-compiler asciidoc

set -e

export GOROOT=/usr/local/go
export GOPATH=/root/.go
export PATH=$GOROOT/bin:$GOPATH/bin:$PATH

mkdir -p $GOPATH

# node.js
wget -qO- https://nodejs.org/dist/latest/node-v4.1.0-linux-x64.tar.gz | tar -C /usr/local -xzf -
ln -s /usr/local/node-v4.1.0-linux-x64/bin/npm /usr/local/bin/npm
ln -s /usr/local/node-v4.1.0-linux-x64/bin/node /usr/local/bin/node

wget -qO- https://storage.googleapis.com/golang/go1.5.1.linux-amd64.tar.gz | tar -C /usr/local -xzf -

# Build runC
go get github.com/tools/godep

mkdir -p $GOPATH/src
mkdir -p $GOPATH/src/github.com
mkdir -p $GOPATH/src/github.com/opencontainers

rm -rf $GOPATH/src/github.com/opencontainers/runc

cd $GOPATH/src/github.com/opencontainers; git clone https://github.com/opencontainers/runc
cd $GOPATH/src/github.com/opencontainers/runc; godep get
cd $GOPATH/src/github.com/opencontainers/runc; make BUILDTAGS=
cd $GOPATH/src/github.com/opencontainers/runc; make install


# Build CRIU
wget -qO- http://download.openvz.org/criu/criu-1.7.tar.bz2 | tar -C /tmp -jxf -
cd /tmp/criu-1.7
make && make install

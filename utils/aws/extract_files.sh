#!/bin/bash

rm -rf dist
mkdir -p dist
cid=XXXXX # docker run
sudo docker cp ${cid}:/nanogi/build/bin/nanogi dist/
sudo docker cp ${cid}:/usr/local/lib/libassimp.so.3 dist/
sudo docker cp ${cid}:/usr/lib/libfreeimage.so.3 dist/
sudo docker cp ${cid}:/usr/local/lib/libembree.so.2 dist/
sudo docker cp ${cid}:/usr/lib/x86_64-linux-gnu/libyaml-cpp.so.0.5 dist
sudo docker cp ${cid}:/usr/lib/libtbb.so.2 dist/
sudo docker cp ${cid}:/usr/lib/libctemplate.so.2 dist/
sudo docker cp ${cid}:/usr/lib/x86_64-linux-gnu/libjpeg.so.8 dist/
sudo docker cp ${cid}:/usr/lib/x86_64-linux-gnu/libopenjpeg.so.2 dist/
sudo docker cp ${cid}:/usr/lib/x86_64-linux-gnu/libraw.so.9 dist/
sudo docker cp ${cid}:/usr/lib/x86_64-linux-gnu/libIex.so.6 dist/
sudo docker cp ${cid}:/usr/lib/x86_64-linux-gnu/libHalf.so.6 dist/
sudo docker cp ${cid}:/usr/lib/x86_64-linux-gnu/libIlmThread.so.6 dist/
sudo docker cp ${cid}:/usr/lib/x86_64-linux-gnu/libIlmImf.so.6 dist/

cwd=`pwd`

cd dist
ln -s libassimp.so.3.1.1 libassimp.so.3
ln -s libfreeimage-3.15.4.so libfreeimage.so.3
ln -s libembree.so.2.5.1 libembree.so.2
ln -s libyaml-cpp.so.0.5.1 libyaml-cpp.so.0.5
ln -s libctemplate.so.2.0.1 libctemplate.so.2
ln -s libIex.so.6.0.0 libIex.so.6
ln -s libHalf.so.6.0.0 libHalf.so.6
ln -s libIlmThread.so.6.0.0 libIlmThread.so.6
ln -s libIlmImf.so.6.0.0 libIlmImf.so.6
ln -s libraw.so.9.0.0 libraw.so.9
ln -s libjpeg.so.8.0.2 libjpeg.so.8
ln -s libopenjpeg-2.1.3.0.so libopenjpeg.so.2
cd ${cwd}

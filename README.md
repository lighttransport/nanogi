
nanogi
====================

A small reference GI renderer.

Introduction
--------------------

Intended to be used as a reference renderer

not focus on the optimization

Dependencies
--------------------

Dependencies with recommended versions.

- **Boost** (>= 1.56.0)
- **Assimp** (>= 3.1.1)
- **FreeImage** (>= 3.15.4)
- **glm** (>= 0.9.3.3)
- **yaml-cpp** (>= 0.5.1)
- **embree** (>= 2.5.0)
- **Intel Threading Building Blocks** (>= 4.3)
- **Qt** (>= 5.4.1) (optional)
- **GLEW** (>= 1.9.0) (optional)

How To Build
--------------------

### Windows

Tested on VS2013 Update 4.  
We prepared pre-built libraries for VS2013 x64 environment in the external repository.

1. Install cmake (>= 3.1).

2. Install Qt (e.g., msvc2013_64_opengl)

3. Install Boost library

4. Move to the repository and run cmake command.
   You need to specify environment variables for

     - **Boost**: BOOST_ROOT, BOOST_INCLUDEDIR, BOOST_LIBRARYDIR
     - **tbb**: TBB_ROOT, TBB_ARCH_PLATFORM
     - **Qt**: QTDIR
   
   e.g.,

     > $ cd (project root)  
     > $ mkdir build  
     > $ BOOST_ROOT="" BOOST_INCLUDEDIR="D:\boost\boost_1_58_0" BOOST_LIBRARYDIR="D:\boost\boost_1_58_0\lib64-msvc-12.0" TBB_ROOT="D:\tbb\tbb43_20150611oss" TBB_ARCH_PLATFORM="intel64" QTDIR="D:\Qt\Qt5.4.2\5.4\msvc2013_64_opengl" cmake -G "Visual Studio 12 2013 Win64" ..

5. Open solution and build.

### Mac OS X

1. Install some dependencies

    > $ sudo ports install gcc49 cmake boost glm tbb google-ctemplate

2. Install embree

2. Build

    > $ cd (project root)  
    > $ mkdir build  
    > $ cmake -DCMAKE_BUILD_TYPE=Release ..

### Linux

Tested on Debian 7 (wheezy).

1. Add testing repository to the source list (and preferences).  
   Some dependencies requires libraries only distributed under testing repository.  
   Of cource you have a option to build and install them from sources.  
   e.g.

    > \# vim /etc/apt/sources.list
    > ...  
    > deb http://ftp.jp.debian.org/debian/ testing main non-free contrib  
    > deb-src http://ftp.jp.debian.org/debian/ testing main non-free contrib
    >
    > deb http://security.debian.org/ testing/updates main contrib non-free  
    > deb-src http://security.debian.org/ testing/updates main contrib non-free
    >  
    >  
    > \# vim /etc/apt/preferences
    > Package: *  
    > Pin: release a=stable  
    > Pin-Priority: 700  
    >
    > Package: *  
    > Pin: release a=testing  
    > Pin-Priority: 650

2. Install some dependencies

    > $ aptitude install libboost1.55-dev libboost-regex1.55-dev libboost-program-options1.55-dev libboost-system1.55-dev libboost-filesystem1.55-dev  
    > $ aptitude install libyaml-cpp-dev/testing libassimp-dev libglm-dev  

3. Download and install embree.  
   e.g.

    > \# aptitude install freeglut3-dev libxmu-dev libxi-dev  
    > $ cd (downloaded source directory)  
    > $ mkdir build  
    > $ cmake -DCMAKE_BUILD_TYPE=Release -D ENABLE_ISPC_SUPPORT=OFF -D RTCORE_TASKING_SYSTEM=INTERNAL ..

4. Move to the repository and run cmake like

    > $ cd (project root)  
    > $ mkdir build  
    > $ BOOST_ROOT="" BOOST_INCLUDEDIR="/usr/include" BOOST_LIBRARYDIR="/usr/lib/x86_64-linux-gnu" cmake -DCMAKE_BUILD_TYPE=Release ..

5. Make.

    > $ make

### Docker

> $ cd nanogi  
> $ sudo docker build -t nanogi .  
> $ sudo docker run nanogi nanogi

Applications
--------------------

### Libraries

All libraries are written as single file library,
intended to be able to modify the library itself easily.

- **nanogi/macros.hpp**
- **nanogi/basic.hpp**
- **nanogi/rt.hpp**
- **nanogi/bpt.hpp**

### Application

- **nanogi**
    + Algorithm
        * Renderer: PT, PT Direct, LT, LT Direct, BPT, PTMNEE
        * BSDF
            - Diffuse
            - Glossy
            - Specular: Reflection, Refraction, Flesnel
        * Emitter 
            - Light: Area, Point, Directional
            - Sensor: Area, Pinhole
    + Config version: 3-5
    + Platform: Windows, Linux
    + Libraries
        * RFMacros ex39
        * RFBasic ex53
        * RFRTCore ex53
        * RFBPTCore ex53

- **nanogi-viewer**
    + Simple scene viewer
    - Config version: 3-5
    - Platform: Windows
    - Libraries
        - RFMacros ex39
        - RFBasic ex42
        - RFRTCore ex44
        - RFBPTCore ex44

Scene Specification
--------------------

Schema

```yaml
TODO
```

License
--------------------

TODO
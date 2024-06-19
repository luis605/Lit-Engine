# Lit-Engine
<img src="https://github.com/luis605/Lit-Engine/blob/main/docs/images/icon.png" width=50% height=50%>
A high-level game engine made with the speed and portability of cpp, powered by raylib.

## Getting Started

### Cloning the Repository

To clone the repository, execute the following command in your terminal or command prompt:

```bash
git clone --recurse-submodules -j8 https://github.com/luis605/Lit-Engine
```

In case the submodules weren't downloaded, run:
```bash
git submodule update --init --recursive
```

# Building and Installing Dependencies
## Linux
Before building the engine, ensure that you have the necessary dependencies installed. These dependencies can be installed by navigating to the `install` directory and running: 
```bash
./install.sh
```
Then, you can build the remaining dependencies by running:
```bash
./build_dependencies.sh
```

# Building, Running, Debugging
## Linux
### Building the Engine
After successfully cloning the repository and installing dependencies, you can proceed to build the Lit-Engine:
 - Build the engine using the following commands:
```bash
make build
```

 - Run the engine:
```bash
make run
```

Alternatively, you can build and run the engine in a single step using:
```bash
make brun
```

### Debugging
If you encounter any issues or bugs and need to debug the engine, you can follow these steps:
1. Initiate the debugger with the following command:
 - `make debug`
2. Alternative is to build and debug the code in a single command:
 - `make bdb`

Please note that if you used `make brun` and the build step wasn't executed, you can force a complete rebuild with the following command:
```bash
make -B brun
```

# Windows
## Dependencies
To compile Lit Engine on Windows, you need to install some dependencies. CMake, Make, git CLI, clang, MSYS2 and some libraries are required to compile every part of the engine. To build the project you will need the MSYS2 MINGW64 console.

### How to install?
1) Make sure you have git installed on your machine. Otherwise, go to https://gitforwindows.org and install the git terminal. It is needed to initialize and update all submodules.

2) Open the Git CMD and clone Lit Engine: `git clone --recurse-submodules -j8 https://github.com/luis605/Lit-Engine`

3) To install the MSYS2 installer, visit https://www.msys2.org/ and follow their installation guide.

4) Lanch MSYS2 MINGW64 and run the following package manager command `pacman -S make cmake nasm diffutils mingw-w64-x86_64-toolchain mingw-w64-x86_64-python mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-bullet msys/msys2-runtime-devel`. If any installation option pops out, use the default option.

5) Open the Lit-Engine directory inside the MSYS2 MINGW console and run:
```bash
cd include/ffmpeg
./configure --disable-iconv --disable-zlib --disable-network --disable-programs --disable-encoders --disable-demuxers --disable-filters --disable-protocols --disable-openssl --disable-libxml2 --disable-indevs --disable-outdevs # It may take a while
cd ../..
```

Finally, follow the build process below.

 - To build all the dependencies, run the following command inside the project's root directory:
```bash
make build_dependencies
```
 - To generate the cmake file and executable, run:
```bash
cd build
cmake -G "Unix Makefiles" .. --fresh
make # make [all/run/debug/bdb/brun/clean_all]
```

## Socials
### Find us here!

[![X(Twitter](https://img.shields.io/badge/X-000000?style=for-the-badge&logo=x&logoColor=white)](https://twitter.com/TheLitEngine)

[![YouTube](https://img.shields.io/badge/YouTube-FF0000?style=for-the-badge&logo=youtube&logoColor=white)](https://www.youtube.com/channel/UCP38rM1LFbABOVdd67p2-NQ)

[![Discord](https://img.shields.io/badge/Discord-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/XqUZQCxrs6)

[![Website](https://img.shields.io/website-up-down-green-red/http/shields.io.svg)](https://litengine.org/)

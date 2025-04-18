<p align="center">
  <img src="https://github.com/luis605/Lit-Engine/blob/main/Docs/images/icon.png" width=30% height=30%>
</p>

Lit Engine is an **open-source high-level game engine** powered by raylib. Made with the speed and portability of C++, Lit Engine allows you to build 3D experiences with minimal effort, and our intuitive interface will quickly launch you into the development world.

**Give a :star: if you find the project useful! Your support helps the project to keep innovating and delivering exciting features.**

![Number of GitHub contributors](https://img.shields.io/github/contributors/luis605/Lit-Engine)
[![Number of GitHub issues that are open](https://img.shields.io/github/issues/luis605/Lit-Engine)](https://github.com/luis605/Lit-Engine/issues)
![Number of GitHub closed issues](https://img.shields.io/github/issues-closed/luis605/Lit-Engine)
![Number of GitHub pull requests that are open](https://img.shields.io/github/issues-pr-raw/luis605/Lit-Engine)
![GitHub commit activity](https://img.shields.io/github/commit-activity/m/luis605/Lit-Engine)
[![Website](https://img.shields.io/website-up-down-green-red/http/shields.io.svg)](https://litengine.org/)
[![Number of GitHub stars](https://img.shields.io/github/stars/luis605/Lit-Engine)](https://github.com/luis605/Lit-Engine/stargazers)

*Lit Engine is in active development. Contributions and feedback are highly appreciated!*

<hr>

# Quickstart
## Cloning the Repository

Do you want to give Lit Engine a quick run on your machine? You can clone the repository by running the following command on your terminal:

```bash
git clone --recurse-submodules --shallow-submodules -j2 https://github.com/luis605/Lit-Engine
```

If the submodules weren't downloaded, you can do so by running:

```bash
git submodule update --init --recursive --depth 1

```

## Building and Installing Dependencies
### Linux

Before building the engine, ensure that you have all the dependencies installed. They can be installed by opening the Install directory and running *install.sh*:

```bash
cd Install
./install.sh
```

Then, you can build the remaining dependencies by running:
```bash
./build_dependencies.sh
```

### Windows

Before building the engine, ensure that you have all the dependencies installed. They can be installed by opening the Install directory and running *install.sh*:

```bash
cd Install
.\install.bat
```

Then, you can build the remaining dependencies by running:
```bash
.\build_dependencies.bat
```

#

After successfully cloning the repository and setting up the project, you can build and run Lit Engine using our **CMake setup**:

```bash
mkdir build
cd build
cmake .. # Linux
cmake .. -G "MinGW Makefiles" --fresh # Windows
make
make run
```

### Debugging
If you encounter any strange behavior and need to debug the engine, you can start the debugger with the following command:
```bash
make debug
```

Make sure you have built the project in debug mode: `cmake .. -DCMAKE_BUILD_TYPE=Debug`

# Screenshots

![Lit Engine Screenshot 1](https://github.com/luis605/Lit-Engine/blob/main/Docs/images/screenshot1.webp?raw=true)

# Documentation

Documentation is available at https://litengine.org/manual.

**Note:** Our documentation isn't yet finished!

# Contributors
<a href="https://github.com/luis605/Lit-Engine/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=luis605/Lit-Engine&max=400&columns=20" />
  <img src="https://us-central1-tooljet-hub.cloudfunctions.net/github" width="0" height="0" />
</a>

# Socials
### Find us here!

[![X(Twitter](https://img.shields.io/badge/X-000000?style=for-the-badge&logo=x&logoColor=white)](https://twitter.com/TheLitEngine)

[![YouTube](https://img.shields.io/badge/YouTube-FF0000?style=for-the-badge&logo=youtube&logoColor=white)](https://www.youtube.com/channel/UCP38rM1LFbABOVdd67p2-NQ)

[![Discord](https://img.shields.io/badge/Discord-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/XqUZQCxrs6)

[![LitEngine](https://img.shields.io/badge/LitEngine-orange?style=for-the-badge&logo=data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy533cub3JnLzIwMDAvc3ZnIiB2aWV3Qm94PSIwIDAgNDkgNDkiIHhtbGnsPSJodHRwOi8vd3d3LnczLm9yZy8xOTk5L3dlaHRlLWxpdmUiPgogIDxwYXRoIGQ9Ik0zMS42NiAzNy4zMyYjM4MzgzOCA0OS42NiAxNi45OSYjMzgzODAwIDI0LjMxNyAwJiM0MWM2LjU0LTYuNTQgMTUuNzEtNy42NyAyMy4xMi0wLjkydjMwLjE2Yy03LjQxLTAuNjgtMTMuNTQtMi44Ni0xOS42Ni02LjU0eiIgZmlsbD0iI2ZmZmZmZiIvPjwvc3ZnPg==)](https://litengine.org)

# License
Check LICENSE.md for more information.

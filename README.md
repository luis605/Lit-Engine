<p align="center">
  <img src="https://github.com/luis605/Lit-Engine/blob/main/media/images/icon.png" width=30% height=30%>
</p>

Lit Engine is an **open-source high-level game engine** made with the speed and portability of C++. It allows you to build 3D experiences with minimal effort, and our intuitive interface will quickly launch you into the development world.

**Give a :star: if you find the project useful! Your support helps the project to keep innovating and delivering exciting features.**

![Number of GitHub contributors](https://img.shields.io/github/contributors/luis605/Lit-Engine)
[![Number of GitHub issues that are open](https://img.shields.io/github/issues/luis605/Lit-Engine)](https://github.com/luis605/Lit-Engine/issues)
![Number of GitHub closed issues](https://img.shields.io/github/issues-closed/luis605/Lit-Engine)
![Number of GitHub pull requests that are open](https://img.shields.io/github/issues-pr-raw/luis605/Lit-Engine)
![GitHub commit activity](https://img.shields.io/github/commit-activity/m/luis605/Lit-Engine)
[![Website](https://img.shields.io/website-up-down-green-red/http/shields.io.svg)](https://litengine.org/)
[![Number of GitHub stars](https://img.shields.io/github/stars/luis605/Lit-Engine)](https://github.com/luis605/Lit-Engine/stargazers)

Lit Engine is in active development. Contributions and feedback are highly appreciated!

<hr>

# 1. Why Lit Engine?
In a world of increasingly complex and bloated game engines, Lit Engine offers a focused and fluid experience. Our mission is to make game development accessible, fast, and fun.

- **Built for Speed**: Get from an idea to a playable prototype faster. By cutting out unnecessary features, we give you the power to focus on what truly matters: building your game.

- **Intuitive by Design**: We prioritize a clean and easy-to-understand interface and API. Lit Engine is perfect for everyone, taking their first steps and for veterans who value a straightforward workflow that doesn't sacrifice power.

- **Free and Open**: Lit Engine is completely free and open, with no hidden costs or royalty fees. We believe powerful game development tools should be available to everyone.

- **You Are in Control**: We provide the core essentials for 3D game creation without locking you into a rigid ecosystem. This gives you the freedom to build and innovate as you see fit.

# 2. Quickstart
## 2.1. Clone the Repository

First, clone the repository and its submodules.

```bash
git clone --recurse-submodules --shallow-submodules -j2 https://github.com/luis605/Lit-Engine
cd Lit-Engine
```

If you cloned without `--recurse-submodules`, you can fetch them with:
```bash
git submodule update --init --recursive --depth 1

```

## 2.2. Install, Build, and Run
### Linux


```bash
# 1. Configure and build the project
mkdir build && cd build
cmake .. -G Ninja
ninja

# 3. Run the engine
ninja run
```


# 3. Socials
### Find us here!

[![YouTube](https://img.shields.io/badge/YouTube-FF0000?style=for-the-badge&logo=youtube&logoColor=white)](https://www.youtube.com/channel/UCP38rM1LFbABOVdd67p2-NQ)

[![Discord](https://img.shields.io/badge/Discord-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/XqUZQCxrs6)

[![LitEngine](https://img.shields.io/badge/LitEngine-orange?style=for-the-badge&logo=data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy533cub3JnLzIwMDAvc3ZnIiB2aWV3Qm94PSIwIDAgNDkgNDkiIHhtbGnsPSJodHRwOi8vd3d3LnczLm9yZy8xOTk5L3dlaHRlLWxpdmUiPgogIDxwYXRoIGQ9Ik0zMS42NiAzNy4zMyYjM4MzgzOCA0OS42NiAxNi45OSYjMzgzODAwIDI0LjMxNyAwJiM0MWM2LjU0LTYuNTQgMTUuNzEtNy42NyAyMy4xMi0wLjkydjMwLjE2Yy03LjQxLTAuNjgtMTMuNTQtMi44Ni0xOS42Ni02LjU0eiIgZmlsbD0iI2ZmZmZmZiIvPjwvc3ZnPg==)](https://litengine.org)

# 4. License
Check [LICENSE](https://github.com/luis605/Lit-Engine/blob/main/LICENSE) for more information.
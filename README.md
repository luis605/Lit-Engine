# Lit-Engine
<img src="https://github.com/luis605/Lit-Engine/blob/main/docs/images/icon.png" width=50% height=50%>
A high-level game engine made with the speed and portability of cpp, powered by raylib.

# Cloning the Repository
To clone the repository, run the following command in your terminal or command prompt:
```cpp
git clone --recurse-submodules -j8 https://luis605@github.com/luis605/Lit-Engine
```
and paste this password:

`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`

# Building and Installing Dependencies
First you will need to install all commands needed to build this project. They are located inside build/ - `cd build`. Then you can run the first command: `./install.sh`
To build all dependencies run this commands: `./build_dependencies.sh`

# Building the Engine
Once you have cloned the repository, install and build all dependencies, you can then proceed. To build the engine, follow these steps:
1. `make build`
2. `make run`

or: `make brun`

If you run into some bug and need to debug you can call the debugger with:
 - `make debug`
 - Or if you want to rebuild the engine and then start the debugger, do `make bdb`

NOTE: If you ran make brun and `build` wasn't called, you can force make with the following command: `make -B brun`

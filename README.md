# Unnamed-Game-Engine
A game engine with the speed and portability of cpp made with raylib.

# Cloning the Repository
To clone the repository, run the following command in your terminal or command prompt:
```cpp
git clone --recurse-submodules -j8 https://luis605@github.com/luis605/Lit-Engine
```
and paste this password:

`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`

# Building Dependencies
First you will need to install all commands needed to build this project, to do that run `./install.sh`
Run this commands to build all dependencies: `./build_dependencies.sh`

# Building the Engine
Once you have cloned the repository, you can build the engine. To do this, follow these steps:
1. `make build`
2. `make run`

or: `make brun`

If you need to debug the engine do:
1. `make bdb`

NOTE: If building wasn't sucessfully do: `make -B brun`

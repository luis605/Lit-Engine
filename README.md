# Lit-Engine
<img src="https://github.com/luis605/Lit-Engine/blob/main/docs/images/icon.png" width=50% height=50%>
A high-level game engine made with the speed and portability of cpp, powered by raylib.

## Getting Started

### Cloning the Repository

To clone the repository, execute the following command in your terminal or command prompt:

```bash
git clone --recurse-submodules -j8 https://github.com/luis605/Lit-Engine
```
Please note that you need appropriate permissions to access this repository.

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

## Windows
You have to install MinGw and cmake manually to continue!
 - To build dependencies run:
```bash
make build_dependencies
```
 - After that, execute:
```bash
cd build
cmake -G "MinGW Makefiles" .. --fresh
```

### Note
It is possible that some dependencies didn't got downloaded when cloning Lit Engine. To fix it, you will need to clone the following repositories manually:
```bash
https://github.com/BalazsJako/ImGuiColorTextEdit.git
https://github.com/BalazsJako/ImGuiColorTextEdit.git
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
 - To generate the cmake file, run:
```bash
cmake -G "MinGW Makefiles" .. --fresh
```
 - Then use make to run, build and debug the engine [The Same as Linux]!
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
Before building the engine, ensure that you have the necessary dependencies installed. These dependencies can be installed by navigating to the `install` directory and running: 
```bash
./install.sh
```
Then, you can build the remaining dependencies by running:
```bash
./build_dependencies.sh
```
# Building, Running, Debugging
## Building the Engine
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

## Debugging
If you encounter any issues or bugs and need to debug the engine, you can follow these steps:
1. Initiate the debugger with the following command:
 - `make debug`
2. Alternative is to build and debug the code in a single command:
 - `make bdb`

Please note that if you used `make brun` and the build step wasn't executed, you can force a complete rebuild with the following command:
```bash
make -B brun
```

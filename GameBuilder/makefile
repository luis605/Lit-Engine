PYTHON_INCLUDE_DIR := $(shell python3 -c "import sys; print(sys.prefix + '/include')")

CXXFLAGS = -pipe -std=c++17 -O3 -funroll-loops -ftree-vectorize -fno-math-errno -freciprocal-math -fvect-cost-model -fgraphite-identity
SRC_FILES = gameSystem.cpp
INCLUDE_DIRS = -I../include -I../include/nlohmann/include -I../include/bullet3/src -L../include/raylib/src
INCLUDE_DIRS += -I../include/raylib/src -L../include/meshoptimizer/build -I../include/meshoptimizer/src -I../include/pybind11/include
LIB_FLAGS = -lavformat -lavcodec -lavutil -lswscale -lswresample -lm -lraylib -lBulletDynamics -lBulletCollision
LIB_FLAGS += -lLinearMath -lpthread -lmeshoptimizer -I$(PYTHON_INCLUDE_DIR)/python3.11 -lpython3.11

build:
	@g++ $(CXXFLAGS) $(SRC_FILES) $(INCLUDE_DIRS) $(LIB_FLAGS) -o ../exported_game/my_program.out

.PHONY: build

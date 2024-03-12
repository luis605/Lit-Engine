CXXFLAGS = -g -pipe -std=c++17 -fpermissive -w -Wall -g
SRC_FILES = gameSystem.cpp
INCLUDE_DIRS = -I../include/ -L../libs -I../include/nlohmann/include -L../include/bullet3/src -I../include/bullet3/src
LIB_FLAGS = -pthread -lpython3.11 -fPIC `python3.11 -m pybind11 --includes` -ldl -lBulletDynamics -lBulletCollision -lLinearMath
LIB_FLAGS += -lavformat -lavcodec -lavutil -lswscale -lz -lm -lpthread -ldrm -L. -lraylib -lmeshoptimizer

build:
	@g++ $(CXXFLAGS) $(SRC_FILES) $(INCLUDE_DIRS) $(LIB_FLAGS) -o ../exported_game/my_program.out

.PHONY: build

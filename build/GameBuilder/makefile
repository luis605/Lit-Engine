GREEN = \033[32m

define echo_success
    @echo "$(GREEN)$(1)\033[0m"
endef

define echo_fail
    @echo "$(GREEN)$(1)\033[0m"
endef


PYTHON_INCLUDE_DIR := $(shell python -c "import sys; print(sys.prefix + '/include')")

CXXFLAGS = -g -pipe -std=c++17 -fpermissive -w -Wall -g
SRC_FILES = gameSystem.cpp
INCLUDE_DIRS = -I../../include/ -I../../include/nlohmann -I../../ffmpeg -I../../include/bullet3/src -I../../include/bullet3/lib -I./pybind11/include -I$(PYTHON_INCLUDE_DIR)
LIB_FLAGS = -pthread -ldl -lBulletDynamics -lBulletCollision -lLinearMath -lavformat -lavcodec -lavutil -lswscale -lz -lm -lpthread -ldrm -L. -lstatic -lraylib -lpybind11

build:
	@g++ $(CXXFLAGS) $(SRC_FILES) $(INCLUDE_DIRS) $(LIB_FLAGS) -o ../exported_game/my_program.out

.PHONY: build print

GREEN = \033[32m
RED = \033[31m
NC = \033[0m

define echo_success
    @echo "$(GREEN)$(1)$(NC)"
endef

define echo_fail
    @echo "$(RED)$(1)$(NC)"
endef

PYTHON_INCLUDE_DIR := $(shell python -c "import sys; print(sys.prefix + '/include')")

CXXFLAGS = -g -pipe -std=c++17 -fpermissive -Wall -w -v -g
SRC_FILES = gameSystem.cpp
INCLUDE_DIRS = -I../../include/ -I../../include/nlohmann -I../../ffmpeg -I../../include/bullet3/src -I../../include/bullet3/lib -I./pybind11/include -I$(PYTHON_INCLUDE_DIR) -I.
LIB_FLAGS = -pthread -ldl -L. -lBulletDynamics -lBulletCollision -lLinearMath -L. -lstatic -lraylib -L. -lavformat -lavcodec -lavutil -lswscale -lz -lm -lpthread -ldrm

build:
	@g++ $(CXXFLAGS) $(SRC_FILES) $(INCLUDE_DIRS) $(LIB_FLAGS) -o ../exported_game/my_program.exe

.PHONY: build

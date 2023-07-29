GREEN = \033[32m


define echo_success
    @echo "$(GREEN)$(1)\033[0m"
endef

define echo_fail
    @echo "$(GREEN)$(1)\033[0m"
endef


CXXFLAGS = -g -pipe -std=c++17 -fpermissive -w -Wall -g
SRC_FILES = gameSystem.cpp
INCLUDE_DIRS = -I../include/ -I./include/nlohmann
LIB_FLAGS = -L./include -lboost_filesystem -lraylib -pthread -lpython3.11 -fPIC `python3.11 -m pybind11 --includes` -ldl

build:
	@g++ $(CXXFLAGS) $(SRC_FILES) $(INCLUDE_DIRS) $(LIB_FLAGS) -o ../exported_game/my_program.out

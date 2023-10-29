GREEN =
define echo_success
    @echo $(1)
endef

define echo_fail
    @echo $(1)
endef

STRING1 ?= "hiiii"
STRING2 ?= ""

CXX = g++
CXXFLAGS = -g -std=c++17 -fpermissive -w -Wall -g
SRC_FILES = gameSystem.cpp
INCLUDE_DIRS = -I../include/ -I./include/nlohmann
LIB_FLAGS = -L./include -lboost_filesystem -lraylib -pthread -lfreetype -ld3d9 -lpython3.11 -lbullet3 -lbulletcollision -lbulletlinearmath

build:
	@$(CXX) $(CXXFLAGS) $(SRC_FILES) $(INCLUDE_DIRS) $(LIB_FLAGS) -o ../exported_game/my_program.exe -DSTRING1=$(STRING1) -DSTRING2=$(STRING2)

print:
	@echo $(STRING1)

.PHONY: build print

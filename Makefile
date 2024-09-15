LIT_VERSION  = 0.0.4.5
OS_NAME      = UNDEFINED

ifeq ($(OS),Windows_NT)
    OS_NAME = WINDOWS
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        OS_NAME = LINUX
    endif
endif

define BANNER_TEXT
 _      _ _     ______             _             
 | |    (_) |   |  ____|           (_)            
 | |     _| |_  | |__   _ __   __ _ _ _ __   ___  
 | |    | | __| |  __| | '_ \ / _` | | '_ \ / _ \ 
 | |____| | |_  | |____| | | | (_| | | | | |  __/ 
 |______|_|\__| |______|_| |_|\__, |_|_| |_|\___| 
                               __/ |              
                              |___/               
endef

export BANNER_TEXT

GREEN = \033[32m

define echo_success
    @echo "$(GREEN)$(1)\033[0m"
endef

CXX = g++
CXXFLAGS = -pipe -std=c++17 -Wno-unused-result

SRC_FILES = include/ImGuiColorTextEdit/TextEditor.o \
            include/rlImGui.o \
            include/ImNodes/ImNodes.o \
            include/ImNodes/ImNodesEz.o

INCLUDE_DIRS = -I./include \
               -I./include/ImGuiColorTextEdit \
               -I./include/ImNodes \
               -I./include/nlohmann/include \
               -I./include/imgui \
               -I./include/bullet3/src \
               -I./include/raylib/src \
               -I./include/meshoptimizer/src

LIB_FLAGS = -lavformat -lavcodec -lavutil -lswscale \
            -lswresample -lm -lraylib -lBulletDynamics \
            -lBulletCollision -lLinearMath -lpthread -lmeshoptimizer

ifeq ($(OS_NAME), LINUX)
    PYTHON_INCLUDE_DIR := $(shell python3 -c "import sys; print(sys.prefix + '/include')")
    LIB_FLAGS += -I$(PYTHON_INCLUDE_DIR)/python3.11 -lpython3.11 -I./include/pybind11/include
endif
ifeq ($(OS_NAME), WINDOWS)
    PYTHON_INCLUDE_DIR := $(shell python3 -c "import sys; print(sys.prefix + '/include')")
    LIB_FLAGS += -I$(PYTHON_INCLUDE_DIR)/python3.11 -lpython3.11 -I./include/pybind11/include
endif

IMGUI_OBJECTS = $(patsubst include/imgui/%.cpp, include/imgui/%.o, $(wildcard include/imgui/*.cpp))

include/imgui/%.o: include/imgui/%.cpp
	@echo "Building Dear ImGUI"
	$(CXX) -std=c++17 -O3 -DIMGUI_IMPL_OPENGL_LOADER_GLAD -c $< -o $@

run:
	@echo "Running Lit Engine"
	@$(call echo_success, $(subst $(newline),\n,$$BANNER_TEXT))
	@./LitEngine

build: $(IMGUI_OBJECTS)
	@$(call echo_success, "Building Demo")
	@$(CXX) $(CXXFLAGS) Engine/main.cpp $(SRC_FILES) $(INCLUDE_DIRS) $(IMGUI_OBJECTS) $(LIB_FLAGS) -o LitEngine
	@$(call echo_success, "Success!")

.PHONY: build

all: CXXFLAGS += -O3 -funroll-loops -ftree-vectorize -fno-math-errno -freciprocal-math -fvect-cost-model -fgraphite-identity
all:
	@make --no-print-directory build -j8
	@make --no-print-directory run

profile: CXXFLAGS += -O3 -funroll-loops -ftree-vectorize -fno-math-errno -freciprocal-math -fvect-cost-model -fgraphite-identity -pg
profile: $(IMGUI_OBJECTS)
	@$(call echo_success, "Building gprof Executable")
	@$(CXX) $(CXXFLAGS) Engine/main.cpp $(SRC_FILES) $(INCLUDE_DIRS) $(IMGUI_OBJECTS) $(LIB_FLAGS) -o profiled
	@$(call echo_success, "Success!")
	./profiled
	gprof ./profiled gmon > analysis.txt
	cat analysis.txt | less

debug:
	@echo "Debugging Lit Engine"
	@$(call echo_success, $(subst $(newline),\n,$$BANNER_TEXT))
	@gdb LitEngine

buildDebug: CXXFLAGS += -g -O0
buildDebug: build debug

buildDependencies: $(IMGUI_OBJECTS)
	@$(call echo_success, "Building Dependencies")
	@cd include/raylib/src && make CUSTOM_CFLAGS+="-DSUPPORT_FILEFORMAT_BMP -DSUPPORT_FILEFORMAT_TGA -DSUPPORT_FILEFORMAT_JPG -DSUPPORT_FILEFORMAT_PSD -DSUPPORT_FILEFORMAT_HDR -DSUPPORT_FILEFORMAT_PIC -DSUPPORT_FILEFORMAT_KTX -DSUPPORT_FILEFORMAT_ASTC -DSUPPORT_FILEFORMAT_PKM -DSUPPORT_FILEFORMAT_PVR -DSUPPORT_FILEFORMAT_SVG" -B
	@cd include/meshoptimizer && mkdir -p build && cd build && cmake .. && make
	@$(CXX) -c $(IMGUI_OBJECTS) -I./include/imgui -O3 include/ImNodes/ImNodes.cpp -o include/ImNodes/ImNodes.o
	@$(CXX) -c $(IMGUI_OBJECTS) -I./include/imgui -O3 include/ImNodes/ImNodesEz.cpp -o include/ImNodes/ImNodesEz.o
	@$(CXX) -c $(IMGUI_OBJECTS) -I./include/imgui -O3 -I./include/raylib/src include/rlImGui.cpp -o include/rlImGui.o
	@$(CXX) -c $(IMGUI_OBJECTS) -I./include/imgui -O3 include/ImGuiColorTextEdit/TextEditor.cpp -o include/ImGuiColorTextEdit/TextEditor.o
	@$(CXX) -c -O3 -I./include/raylib/src include/rlFrustum.cpp -o include/rlFrustum.o

clean:
	-find . -name "*.out" | xargs rm
	-find . -name "*.o" | xargs rm

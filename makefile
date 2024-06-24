# NOTES:
# You will need to install ccache in your computer as well as gcc
# To run Lit Engine type "make brun". brun stands for build&run
# To clean up  all .out and .o file type "make clean"

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

UNAME := $(shell uname)

CXX := g++

CXXFLAGS = -pipe -std=c++17 -Wno-unused-result
SRC_FILES = include/ImGuiColorTextEdit/TextEditor.o include/rlImGui.o include/ImNodes/ImNodes.o include/ImNodes/ImNodesEz.o
INCLUDE_DIRS = -I./include -I./include/ImGuiColorTextEdit -I./include/ImNodes -I./include/ffmpeg -I./include/nlohmann/include
INCLUDE_DIRS += -I./include/imgui -I./include/bullet3/src -L./include/raylib/src -I./include/raylib/src -L./include/meshoptimizer/build -I./include/meshoptimizer/src
LIB_FLAGS = -lraylib -lBulletDynamics -lBulletCollision -lLinearMath -lpthread -lmeshoptimizer

ifeq ($(UNAME), Linux)
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
	@./lit_engine.out

build: $(IMGUI_OBJECTS)
	@$(call echo_success, "Building Demo")
	@$(CXX) $(CXXFLAGS) main.cpp $(SRC_FILES) $(INCLUDE_DIRS) $(IMGUI_OBJECTS) $(LIB_FLAGS) -Wl,-rpath,'$$ORIGIN:.' -lavformat -lavcodec -lavutil -lswscale -lswresample -o lit_engine.out
	@$(call echo_success, "Success!")

brun: CXXFLAGS += -O3 -funroll-loops -ftree-vectorize -fno-math-errno -freciprocal-math -fvect-cost-model -fgraphite-identity
brun:
	@make --no-print-directory build -j8
	@make --no-print-directory run

debug:
	@echo "Debugging Lit Engine"
	@$(call echo_success, $(subst $(newline),\n,$$BANNER_TEXT))
	@gdb lit_engine.out

bdb: CXXFLAGS += -g -O0
bdb: build debug

build_dependencies: $(IMGUI_OBJECTS)
	@$(call echo_success, "Building Dependencies")
	@$(CXX) -c $(IMGUI_OBJECTS) -I./include/imgui -O3 include/ImNodes/ImNodes.cpp -o include/ImNodes/ImNodes.o
	@$(CXX) -c $(IMGUI_OBJECTS) -I./include/imgui -O3 include/ImNodes/ImNodesEz.cpp -o include/ImNodes/ImNodesEz.o
	@$(CXX) -c $(IMGUI_OBJECTS) -I./include/imgui -O3 -I./include/raylib/src include/rlImGui.cpp -o include/rlImGui.o
	@$(CXX) -c $(IMGUI_OBJECTS) -I./include/imgui -O3 include/ImGuiColorTextEdit/TextEditor.cpp -o include/ImGuiColorTextEdit/TextEditor.o
	@$(CXX) -c -O3 -I./include/raylib/src include/rlFrustum.cpp -o include/rlFrustum.o

clean:
	-find . -name "*.out" | xargs rm
	-find . -name "*.o" | xargs rm

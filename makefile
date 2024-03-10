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


CXXFLAGS = -g -pipe -std=c++17 -fpermissive -w -Wall -DNDEBUG -O0 -g
SRC_FILES = include/ImGuiColorTextEdit/TextEditor.o include/rlImGui.o include/ImNodes/ImNodes.o include/ImNodes/ImNodesEz.o
INCLUDE_DIRS = -I./include -L.include/ -I./include/ImGuiColorTextEdit -L./include/ffmpeg -I./include/ffmpeg -I./include/nlohmann/include -I./include/imgui -L/include/bullet3/src -I./include/bullet3/src -L./include/raylib/src -I./include/raylib/src -L./libs/
LIB_FLAGS = -lraylib -ldl -lBulletDynamics -lBulletCollision -lLinearMath
LIB_FLAGS += -lavformat -lavcodec -lavutil -lswscale -lswresample -lz -lm -lpthread -ldrm -ltbb -lmeshoptimizer

PYTHON_INCLUDE_DIR := $(shell python -c "import sys; print(sys.prefix + '/include')")

LIB_FLAGS_LINUX = $(LIB_FLAGS) -lpython3.11 -fPIC `python3.11 -m pybind11 --includes`

LIB_FLAGS_WINDOWS = -I./include/pybind11/include -I$(PYTHON_INCLUDE_DIR) $(LIB_FLAGS)



IMGUI_OBJECTS = $(patsubst include/imgui/%.cpp, include/imgui/%.obj, $(wildcard include/imgui/*.cpp))

include/imgui/%.obj: include/imgui/%.cpp
	@echo "Building Dear ImGUI"
	g++ -std=c++17 -O3 -DIMGUI_IMPL_OPENGL_LOADER_GLAD -c $< -o $@


run:
	@echo "Running Lit Engine"
	@$(call echo_success, $(subst $(newline),\n,$$BANNER_TEXT))
	@./run.sh


build: $(IMGUI_OBJECTS)
	@$(call echo_success, "Building Demo")
	@g++ $(CXXFLAGS) main.cpp $(SRC_FILES) $(INCLUDE_DIRS) $(IMGUI_OBJECTS) $(LIB_FLAGS_LINUX) -Wl,-rpath,'$$ORIGIN:.' -lavformat -lavcodec -lavutil -lswscale -lswresample -o lit_engine.out
	@$(call echo_success, "Success!")


brun:
	@make --no-print-directory build -j8
	@make --no-print-directory run

sandbox: $(IMGUI_OBJECTS)
	@g++ -g $(IMGUI_OBJECTS) sandbox.cpp -o sandbox.out -L. include/rlImGui.o -lraylib -Wall -w  -I./include/imgui -L. -lmeshoptimizer
	@./sandbox.out

debug:
	@echo "Debugging Lit Engine"
	@echo $(info $(BANNER_TEXT))
	export LD_LIBRARY_PATH=./libs/:$LD_LIBRARY_PATH
	@gdb lit_engine.out

bdb: build debug

build_dependencies: $(IMGUI_OBJECTS)
	@$(call echo_success, "Building Dependencies")
	@g++ -c $(IMGUI_OBJECTS) -I./include/imgui -O3 include/ImNodes/ImNodes.cpp -o include/ImNodes/ImNodes.o
	@g++ -c $(IMGUI_OBJECTS) -I./include/imgui -O3 include/ImNodes/ImNodesEz.cpp -o include/ImNodes/ImNodesEz.o
	@g++ -c $(IMGUI_OBJECTS) -I./include/imgui -O3 include/rlImGui.cpp -o include/rlImGui.o
	@g++ -c $(IMGUI_OBJECTS) -I./include/imgui -O3 include/ImGuiColorTextEdit/TextEditor.cpp -o include/ImGuiColorTextEdit/TextEditor.o
	@g++ -c -O3 include/rlFrustum.cpp -o include/rlFrustum.o


clean:
	-find . -name "*.out" | xargs rm
	-find . -name "*.o" | xargs rm
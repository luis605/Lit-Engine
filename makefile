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


CXXFLAGS = -g -pipe -std=c++17 -fpermissive -w -Wall -DNDEBUG -O0
SRC_FILES = ImGuiColorTextEdit/TextEditor.o include/rlImGui.o ImNodes/ImNodes.o ImNodes/ImNodesEz.o
INCLUDE_DIRS = -I./include -I./ImGuiColorTextEdit -L./ffmpeg -L. -I. -I./ffmpeg -I./include/nlohmann -L./include -I./imgui -L/include/bullet3/src
INCLUDE_DIRS_STATIC = -I./include -I/usr/local/lib -I./include/nlohmann -I./include/bullet3/src -I./ffmpeg -I./ffmpeg -L./include
LIB_FLAGS = -L./include -lboost_filesystem -lraylib -ldl -lBulletDynamics -lBulletCollision -lLinearMath -I./include/bullet3/src
LIB_FLAGS += -L./ffmpeg -lavformat -lavcodec -lavutil -lswscale -lswresample -lz -lm -lpthread -ldrm -ltbb -lmeshoptimizer -L./libs/

PYTHON_INCLUDE_DIR := $(shell python -c "import sys; print(sys.prefix + '/include')")

LIB_FLAGS_LINUX = $(LIB_FLAGS) -lpython3.11 -fPIC `python3.11 -m pybind11 --includes`

LIB_FLAGS_WINDOWS = -I./pybind11/include -I$(PYTHON_INCLUDE_DIR) $(LIB_FLAGS)



IMGUI_OBJECTS = $(patsubst imgui/%.cpp, imgui/%.obj, $(wildcard imgui/*.cpp))

imgui/%.obj: imgui/%.cpp
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


static-build-windows:
	@echo "Building Static"
	@del /F /Q libstatic.a static.o
	@g++ $(CXXFLAGS) static.cpp -o static.o $(INCLUDE_DIRS_STATIC) $(LIB_FLAGS_WINDOWS) -c
	@llvm-ar rcs libstatic.a static.o



static-build-linux:
	@$(call echo_success, "Building Static")
	@rm -f libstatic.a static.o
	@g++ $(CXXFLAGS) static.cpp -o static.o $(INCLUDE_DIRS_STATIC) $(LIB_FLAGS_LINUX) -c
	@ar rcs libstatic.a static.o



IMGUI_OBJECTS = $(patsubst imgui/%.cpp, imgui/%.obj, $(wildcard imgui/*.cpp))



sandbox: $(IMGUI_OBJECTS)
	@g++ -g $(IMGUI_OBJECTS) sandbox.cpp -o sandbox.out -L. include/rlImGui.o -lraylib -Wall -w  -I./imgui -L. -lmeshoptimizer
	@./sandbox.out



debug:
	@echo "Debugging Lit Engine"
	@echo $(info $(BANNER_TEXT))


	@gdb lit_engine.out

bdb: build debug

build_tests:
	@ccache g++ $(CXXFLAGS) tests.cpp include/rlImGui.o $(INCLUDE_DIRS) $(IMGUI_OBJECTS) $(LIB_FLAGS) -o tests.out

tests:
	@make --no-print-directory build_tests -j10
	@./tests.out

# sandbox:
# 	@ccache g++ $(CXXFLAGS) sandbox.cpp $(SRC_FILES) $(INCLUDE_DIRS) $(IMGUI_OBJECTS) $(LIB_FLAGS) -o sandbox.out
# 	@./sandbox.out

build_dependencies: $(IMGUI_OBJECTS)
	@$(call echo_success, "Building Dependencies")
	@g++ -c $(IMGUI_OBJECTS) -I./imgui -O3 ImNodes/ImNodes.cpp -o ImNodes/ImNodes.o
	@g++ -c $(IMGUI_OBJECTS) -I./imgui -O3 ImNodes/ImNodesEz.cpp -o ImNodes/ImNodesEz.o
	@g++ -c $(IMGUI_OBJECTS) -I./imgui -O3 include/rlImGui.cpp -o include/rlImGui.o
	@g++ -c $(IMGUI_OBJECTS) -I./imgui -O3 ImGuiColorTextEdit/TextEditor.cpp -o ImGuiColorTextEdit/TextEditor.o
	@g++ -c -O3 include/rlFrustum.cpp -o include/rlFrustum.o


clean:
	-find . -name "*.out" | xargs rm
	-find . -name "*.o" | xargs rm
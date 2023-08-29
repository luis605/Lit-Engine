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





CXXFLAGS = -g -pipe -flto -fuse-ld=gold -std=c++17 -fpermissive -w -Wall -DNDEBUG -O0
SRC_FILES = main.cpp ImGuiColorTextEdit/TextEditor.o include/rlImGui.o ImNodes/ImNodes.o ImNodes/ImNodesEz.o
INCLUDE_DIRS = -I./include -I./ImGuiColorTextEdit -I/usr/local/lib -L/usr/lib/x86_64-linux-gnu/ -L/usr/local/lib -I./include/nlohmann -I./imgui -L/include/bullet3/src
LIB_FLAGS = -L./include -lboost_filesystem -lraylib -pthread -lpython3.11 -fPIC `python3.11 -m pybind11 --includes` -ldl -lBulletDynamics -lBulletCollision -lLinearMath -I./include/bullet3/src


IMGUI_OBJECTS = $(patsubst imgui/%.cpp, imgui/%.o, $(wildcard imgui/*.cpp))

imgui/%.o: imgui/%.cpp
	@echo "Building Dear ImGUI"
	g++ -std=c++17 -O3 -DIMGUI_IMPL_OPENGL_LOADER_GLAD -c $< -o $@


run:
	@echo "Running Lit Engine"
	@$(call echo_success, $(subst $(newline),\n,$$BANNER_TEXT))
	@./lit_engine.out


build: $(IMGUI_OBJECTS)
	@$(call echo_success, "Building Demo")
	@ccache g++ $(CXXFLAGS) $(SRC_FILES) $(INCLUDE_DIRS) $(IMGUI_OBJECTS) $(LIB_FLAGS) -o lit_engine.out


brun:
	@make --no-print-directory build -j8
	@make --no-print-directory run

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

build_dependencies: $(IMGUI_OBJECTS)
	@$(call echo_success, "Building Dependencies")
	@g++ -c $(IMGUI_OBJECTS) -I./imgui -O3 ImNodes/ImNodes.cpp -o ImNodes/ImNodes.o
	@g++ -c $(IMGUI_OBJECTS) -I./imgui -O3 ImNodes/ImNodesEz.cpp -o ImNodes/ImNodesEz.o
	@g++ -c $(IMGUI_OBJECTS) -I./imgui -O3 include/rlImGui.cpp -o include/rlImGui.o
	@g++ -c $(IMGUI_OBJECTS) -I./imgui -O3 ImGuiColorTextEdit/TextEditor.cpp -o ImGuiColorTextEdit/TextEditor.o



clean:
	-find . -name "*.out" | xargs rm
	-find . -name "*.o" | xargs rm

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






CXXFLAGS = -g -pipe -std=c++17 -O0 -fpermissive -w -Wall -DNDEBUG
SRC_FILES = test_imgui.cpp include/rlImGui.cpp ImGuiColorTextEdit/TextEditor.cpp
INCLUDE_DIRS = -I./include -I./ImGuiColorTextEdit -I/usr/local/lib -L/usr/lib/x86_64-linux-gnu/ -L/usr/local/lib -I./include/nlohmann -I./imgui
LIB_FLAGS = -L./include -lboost_filesystem -lraylib -pthread -lpython3.11 -fPIC `python3.11 -m pybind11 --includes` -ldl -lPhysX_static_64 -lPhysXCommon_static_64 -lPhysXFoundation_static_64
OTHER_FILES = ./include/libPhysX_static_64.a ./include/libPhysXCommon_static_64.a ./include/libPhysXFoundation_static_64.a

IMGUI_OBJECTS = $(patsubst %.cpp, %.o, $(wildcard imgui/*.cpp))

imgui/*.o: imgui/*.cpp
	@echo "Building Dear ImGUI"
	g++ -std=c++17 -O3 -DIMGUI_IMPL_OPENGL_LOADER_GLAD -c $< -o $@

run:
	@echo "Running Lit Engine"
	@$(call echo_success, $(subst $(newline),\n,$$BANNER_TEXT))
	@./lit_engine.out

build: $(IMGUI_OBJECTS)
	@$(call echo_success, "Building Demo")
	@ccache g++ $(CXXFLAGS) $(SRC_FILES) $(INCLUDE_DIRS) $(IMGUI_OBJECTS) $(LIB_FLAGS) -o lit_engine.out $(OTHER_FILES)


brun:
	@make --no-print-directory build -j24
	@make --no-print-directory run

debug:
	@echo "Debugging Lit Engine"
	@echo $(info $(BANNER_TEXT))


	@gdb lit_engine.out

bdb: build debug

do_tests: $(IMGUI_OBJECTS)
	@echo "Building Tests"
	@ccache g++ -g -flto -std=c++20 -O3 -DNDEBUG -march=native do_tests.cpp -I/usr/include/python3.11/ -lpython3.11 -fPIC `python3.11 -m pybind11 --includes` -o do_tests.out -ltbb -ltbb -lpthread -fpermissive -w -Wall
	@echo "Running Tests"
	@./do_tests.out

do_testsv2:
	g++ -I/usr/include/bsd/ testing.cpp -o do_tests.out
	./tests.out

physics_demo:
	@echo "Building Demo"
	@ccache g++ -flto physics.cpp -std=c++17 -I./include -I./bullet3 $(LDLIBS) -O3 -lraylib -o physics_demo.out -fpermissive -w
	@echo "Running Physics Demo"
	@./physics_demo.out


clean:
	-find . -name "*.out" | xargs rm
	-find . -name "*.o" | xargs rm

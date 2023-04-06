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

export LD_LIBRARY_PATH=./physx-include:$LD_LIBRARY_PATH

IMGUI_OBJECTS = $(patsubst %.cpp, %.o, $(wildcard imgui/*.cpp))
LDLIBS=-lBulletDynamics -lBulletCollision -lLinearMath -lraylib

imgui/*.o: imgui/*.cpp
	@echo "Building Dear ImGUI"
	g++ -std=c++17 -O3 -DIMGUI_IMPL_OPENGL_LOADER_GLAD -c $< -o $@



build: $(IMGUI_OBJECTS)
	@echo "Building Demo"
	@ccache g++ -g -flto test_imgui.cpp include/rlImGui.cpp ImGuiColorTextEdit/TextEditor.cpp $(IMGUI_OBJECTS) -std=c++17 -I/usr/include/python3.11/ -lpython3.11 -fPIC `python3.11 -m pybind11 --includes` -I./include -I./ImGuiColorTextEdit -I/usr/local/lib -I./include/nlohmann -lboost_filesystem -O3 -lraylib -Iimgui -pthread -o lit_engine.out -fpermissive -w -Wall

run:
	@echo "Running Lit Engine"
	@echo $(info $(BANNER_TEXT))
	@./lit_engine.out


brun: build run

debug:
	@echo "Debugging Lit Engine"
	@echo $(info $(BANNER_TEXT))


	@gdb lit_engine.out

bdb: build debug

do_tests: $(IMGUI_OBJECTS)
	@echo "Building Tests"
	@ccache g++ -g -flto do_tests.cpp include/rlImGui.cpp ImGuiColorTextEdit/TextEditor.cpp $(IMGUI_OBJECTS) -std=c++17 -I./include -I./ImGuiColorTextEdit -O3 -lraylib -Iimgui -pthread -o do_tests.out -fpermissive -w -Wall
	@echo "Running Tests"
	@./do_tests.out



physics_demo:
	@echo "Building Demo"
	@ccache g++ -flto physics.cpp -std=c++17 -I./include -I./bullet3 $(LDLIBS) -O3 -lraylib -o physics_demo.out -fpermissive -w
	@echo "Running Physics Demo"
	@./physics_demo.out


clean:
	-find . -name "*.out" | xargs rm
	-find . -name "*.o" | xargs rm

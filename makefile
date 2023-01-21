IMGUI_OBJECTS = $(patsubst %.cpp, %.o, $(wildcard imgui/*.cpp))


imgui/*.o: imgui/*.cpp
	@echo "Building Dear ImGUI"
	g++ -std=c++17 -O3 -DIMGUI_IMPL_OPENGL_LOADER_GLAD -c $< -o $@

timgui: $(IMGUI_OBJECTS)
	@echo "Building Demo"
	@g++ test_imgui.cpp rlImGui.cpp $(IMGUI_OBJECTS) -std=c++17 -I/usr/local/include/ -lboost_filesystem -O3 -lraylib -Iimgui -I/usr/include/SDL2 -lSDL2 -o my_project -fpermissive -w
	@echo "Running my_project"
	@./my_project


brun:
	g++ main.cpp -std=c++17 -lraylib -lGL -lm -lpthread -lncurses -ldl -lrt -lX11 -lpython3.11 -fpermissive -w
	./a.out

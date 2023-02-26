IMGUI_OBJECTS = $(patsubst %.cpp, %.o, $(wildcard imgui/*.cpp))

imgui/*.o: imgui/*.cpp
	@echo "Building Dear ImGUI"
	g++ -std=c++17 -O3 -DIMGUI_IMPL_OPENGL_LOADER_GLAD -c $< -o $@


timgui: $(IMGUI_OBJECTS)
	@echo "Building Demo"
	@g++ test_imgui.cpp include/rlImGui.cpp $(IMGUI_OBJECTS) -std=c++17 -I/usr/include/python3.11/ -lpython3.11 -fPIC `python3.11 -m pybind11 --includes` -I/usr/local/include/ -I./include -lboost_filesystem -O3 -lraylib -Iimgui -o lit_engine.out -fpermissive -w
	@echo "Running Lit Engine"
	@./lit_engine.out




hi: $(IMGUI_OBJECTS)
	@echo "Building Demo"
	@g++ hi.cpp -std=c++17 -I/usr/include/python3.11/ -lpython3.11 -fPIC `python3.11 -m pybind11 --includes` -O3 -o hi.out -fpermissive -w
	@echo "Running Hi"
	@./hi.out




brun:
	g++ main.cpp -std=c++17 -I/usr/include/eigen3 -I./libigl/include/ -L/usr/local/lib -lraylib -lpython3.11 -fpermissive -w
	./a.out


to:
	g++ test_others.cpp -std=c++17 -I/usr/include/eigen3 -I./libigl/include/ -L/usr/local/lib -lraylib -lpython3.11 -fpermissive -w
	./a.out



st:
	g++ shader_test.cpp -std=c++17 -L/usr/local/lib -lraylib
	./a.out

2w:
	g++ 2windows.cpp -std=c++17 -Linclude -lraylib -fpermissive -w
	./a.out


clean:
	-find . -name "*.out" | xargs rm
	-find . -name "*.o" | xargs rm

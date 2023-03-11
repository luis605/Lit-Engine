IMGUI_OBJECTS = $(patsubst %.cpp, %.o, $(wildcard imgui/*.cpp))
LDLIBS=-lBulletDynamics -lBulletCollision -lLinearMath -lraylib

imgui/*.o: imgui/*.cpp
	@echo "Building Dear ImGUI"
	g++ -std=c++17 -O3 -DIMGUI_IMPL_OPENGL_LOADER_GLAD -c $< -o $@


timgui: $(IMGUI_OBJECTS)
	@echo "Building Demo"
	@ccache g++ -flto test_imgui.cpp include/rlImGui.cpp $(IMGUI_OBJECTS) -std=c++17 -I/usr/include/python3.11/ -lpython3.11 -fPIC `python3.11 -m pybind11 --includes` -I/usr/local/include/ -I./include -I./include/nlohmann -lboost_filesystem -O3 -lraylib -Iimgui -o lit_engine.out -fpermissive -w
	@echo "Running Lit Engine"
	@./lit_engine.out


do_tests: $(IMGUI_OBJECTS)
	@echo "Building Demo"
	@ccache g++ -flto do_tests.cpp include/rlImGui.cpp $(IMGUI_OBJECTS) -std=c++17 -I/usr/include/python3.11/ -lpython3.11 -fPIC `python3.11 -m pybind11 --includes` -I/usr/local/include/ -I./include -I./include/nlohmann -lboost_filesystem -O3 -lraylib -Iimgui -o do_tests.out -fpermissive -w
	@echo "Running Lit Engine"
	@./do_tests.out


physics_demo:
	@echo "Building Demo"
	@ccache g++ -flto physics.cpp -std=c++17 -I./include -I./bullet3 $(LDLIBS) -O3 -lraylib -o physics_demo.out -fpermissive -w
	@echo "Running Physics Demo"
	@./physics_demo.out





brun:
	g++ main.cpp -std=c++17 -I/usr/include/eigen3 -I./libigl/include/ -L/usr/local/lib -lraylib -lpython3.11 -fpermissive -w
	./a.out



clean:
	-find . -name "*.out" | xargs rm
	-find . -name "*.o" | xargs rm

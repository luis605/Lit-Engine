
define BANNER_TEXT


                                                                  ddddddddbbbbbbbb                                               
   SSSSSSSSSSSSSSS                                                d::::::db::::::b                                               
 SS:::::::::::::::S                                               d::::::db::::::b                                               
S:::::SSSSSS::::::S                                               d::::::db::::::b                                               
S:::::S     SSSSSSS                                               d:::::d  b:::::b                                               
S:::::S              aaaaaaaaaaaaa  nnnn  nnnnnnnn        ddddddddd:::::d  b:::::bbbbbbbbb       ooooooooooo xxxxxxx      xxxxxxx
S:::::S              a::::::::::::a n:::nn::::::::nn    dd::::::::::::::d  b::::::::::::::bb   oo:::::::::::oox:::::x    x:::::x 
 S::::SSSS           aaaaaaaaa:::::an::::::::::::::nn  d::::::::::::::::d  b::::::::::::::::b o:::::::::::::::ox:::::x  x:::::x  
  SS::::::SSSSS               a::::ann:::::::::::::::nd:::::::ddddd:::::d  b:::::bbbbb:::::::bo:::::ooooo:::::o x:::::xx:::::x   
    SSS::::::::SS      aaaaaaa:::::a  n:::::nnnn:::::nd::::::d    d:::::d  b:::::b    b::::::bo::::o     o::::o  x::::::::::x    
       SSSSSS::::S   aa::::::::::::a  n::::n    n::::nd:::::d     d:::::d  b:::::b     b:::::bo::::o     o::::o   x::::::::x     
            S:::::S a::::aaaa::::::a  n::::n    n::::nd:::::d     d:::::d  b:::::b     b:::::bo::::o     o::::o   x::::::::x     
            S:::::Sa::::a    a:::::a  n::::n    n::::nd:::::d     d:::::d  b:::::b     b:::::bo::::o     o::::o  x::::::::::x    
SSSSSSS     S:::::Sa::::a    a:::::a  n::::n    n::::nd::::::ddddd::::::dd b:::::bbbbbb::::::bo:::::ooooo:::::o x:::::xx:::::x   
S::::::SSSSSS:::::Sa:::::aaaa::::::a  n::::n    n::::n d:::::::::::::::::d b::::::::::::::::b o:::::::::::::::ox:::::x  x:::::x  
S:::::::::::::::SS  a::::::::::aa:::a n::::n    n::::n  d:::::::::ddd::::d b:::::::::::::::b   oo:::::::::::oox:::::x    x:::::x 
 SSSSSSSSSSSSSSS     aaaaaaaaaa  aaaa nnnnnn    nnnnnn   ddddddddd   ddddd bbbbbbbbbbbbbbbb      ooooooooooo xxxxxxx      xxxxxxx
                                                                                                                                 
                    

            

endef





export LD_LIBRARY_PATH=./physx-include:$LD_LIBRARY_PATH

IMGUI_OBJECTS = $(patsubst %.cpp, %.o, $(wildcard imgui/*.cpp))
LDLIBS=-lBulletDynamics -lBulletCollision -lLinearMath -lraylib

imgui/*.o: imgui/*.cpp
	@echo "Building Dear ImGUI"
	g++ -std=c++17 -O3 -DIMGUI_IMPL_OPENGL_LOADER_GLAD -c $< -o $@



parenting:
	@echo "Building Demo"
	@ccache g++ -g -flto parenting/parenting.cpp ../include/rlImGui.cpp $(IMGUI_OBJECTS) -std=c++17 -I./../include -O3 -lraylib -Iimgui -pthread -o parenting/parenting.out -fpermissive -w -Wall
	@echo $(info $(BANNER_TEXT))
	@./parenting/parenting.out

clean:
	-find . -name "*.out" | xargs rm
	-find . -name "*.o" | xargs rm
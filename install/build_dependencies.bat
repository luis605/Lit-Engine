cd ..
cd raylib
mkdir build
cd build
cmake ..
cmake --build . --config Release

cd ../..

copy raylib\build\src\raylib_static.lib lib\raylib.lib
copy raylib\src\raylib.h include\
copy raylib\src\raymath.h include\
copy raylib\src\rlgl.h include\
copy raylib\src\rcamera.h include\
copy raylib\src\utils.h include\

copy ImNodes\ImNodes.h include\
copy ImNodes\ImNodesEz.h include\

make build_dependencies

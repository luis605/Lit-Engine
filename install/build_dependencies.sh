cd ..
cd raylib
mkdir build
cd build
cmake ..
cmake --build .
sudo make install

cd ../..

cp raylib/build/raylib/include/raylib.h include/
cp raylib/build/raylib/include/raymath.h include/
cp raylib/build/raylib/include/rlgl.h include/
cp raylib/src/rcamera.h include/
cp raylib/src/utils.h include/

cp ImNodes/ImNodes.h include/
cp ImNodes/ImNodesEz.h include/

make build_dependencies

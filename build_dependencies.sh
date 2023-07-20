cd raylib
mkdir build
cd build
cmake ..
cmake --build .
sudo make install

cd ../..
sudo apt-get install libglfw3-dev libgl1-mesa-dev
export CMAKE_PREFIX_PATH="/usr/include/GL"
cd ImNodes
mkdir build
cd build
cmake ..
make

cd ../include/bullet3
mkdir -p build && cd build
cmake .. -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
mkdir libs
find . -type f -name "*.a" -exec mv {} ./libs/ \;

cd ../../NodeEditor
cmake -S examples -B build -G "Unix Makefiles"
cmake --build build --config Release

cd ../raylib/src
make GRAPHICS=GRAPHICS_API_OPENGL_43 CUSTOM_CFLAGS+="-DSUPPORT_FILEFORMAT_BMP -DSUPPORT_FILEFORMAT_TGA -DSUPPORT_FILEFORMAT_JPG -DSUPPORT_FILEFORMAT_PSD -DSUPPORT_FILEFORMAT_HDR -DSUPPORT_FILEFORMAT_PIC -DSUPPORT_FILEFORMAT_KTX -DSUPPORT_FILEFORMAT_ASTC -DSUPPORT_FILEFORMAT_PKM -DSUPPORT_FILEFORMAT_PVR -DSUPPORT_FILEFORMAT_SVG" -B

cd ../../

cd meshoptimizer
mkdir build
cd build
cmake ..
make
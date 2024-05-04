cd ..
cd include/raylib/src
make CUSTOM_CFLAGS+="-DSUPPORT_FILEFORMAT_BMP -DSUPPORT_FILEFORMAT_TGA -DSUPPORT_FILEFORMAT_JPG -DSUPPORT_FILEFORMAT_PSD -DSUPPORT_FILEFORMAT_HDR -DSUPPORT_FILEFORMAT_PIC -DSUPPORT_FILEFORMAT_KTX -DSUPPORT_FILEFORMAT_ASTC -DSUPPORT_FILEFORMAT_PKM -DSUPPORT_FILEFORMAT_PVR -DSUPPORT_FILEFORMAT_SVG" -B

cd ../../meshoptimizer
mkdir build
cd build
cmake ..
make

cd ../../ffmpeg
./configure --disable-iconv --disable-zlib --disable-network --disable-programs --disable-encoders  --disable-muxers --disable-demuxers --disable-filters --disable-protocols --disable-openssl --disable-libxml2 --disable-indevs --disable-outdevs
make

cd ../../../
copy include/raylib/src/libraylib.a

make build_dependencies

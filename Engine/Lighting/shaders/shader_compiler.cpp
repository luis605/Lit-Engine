#include <spirv_cross/spirv_glsl.hpp>

std::string "lighting_fragment.glsl";
spirv_cross::CompilerGLSL compiler(spirvBinary, binarySize);
spirv_cross::CompilerGLSL::Options options;
compiler.set_common_options(options);
glslSourceCode = compiler.compile();
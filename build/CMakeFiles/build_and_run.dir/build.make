# CMAKE generated file: DO NOT EDIT!
# Generated by "MinGW Makefiles" Generator, CMake Version 3.27

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

SHELL = cmd.exe

# The CMake executable.
CMAKE_COMMAND = "C:\Program Files\CMake\bin\cmake.exe"

# The command to remove a file.
RM = "C:\Program Files\CMake\bin\cmake.exe" -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = C:\Users\USER\Desktop\_\Lit-Engine

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = C:\Users\USER\Desktop\_\Lit-Engine\build

# Utility rule file for build_and_run.

# Include any custom commands dependencies for this target.
include CMakeFiles/build_and_run.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/build_and_run.dir/progress.make

CMakeFiles/build_and_run:
	make --no-print-directory build -j8 && make --no-print-directory run

build_and_run: CMakeFiles/build_and_run
build_and_run: CMakeFiles/build_and_run.dir/build.make
.PHONY : build_and_run

# Rule to build all files generated by this target.
CMakeFiles/build_and_run.dir/build: build_and_run
.PHONY : CMakeFiles/build_and_run.dir/build

CMakeFiles/build_and_run.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles\build_and_run.dir\cmake_clean.cmake
.PHONY : CMakeFiles/build_and_run.dir/clean

CMakeFiles/build_and_run.dir/depend:
	$(CMAKE_COMMAND) -E cmake_depends "MinGW Makefiles" C:\Users\USER\Desktop\_\Lit-Engine C:\Users\USER\Desktop\_\Lit-Engine C:\Users\USER\Desktop\_\Lit-Engine\build C:\Users\USER\Desktop\_\Lit-Engine\build C:\Users\USER\Desktop\_\Lit-Engine\build\CMakeFiles\build_and_run.dir\DependInfo.cmake "--color=$(COLOR)"
.PHONY : CMakeFiles/build_and_run.dir/depend

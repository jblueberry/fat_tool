# use c++ 20 to compile main.cpp
env = Environment(CXXFLAGS='-std=c++20')
# build fat_manager as a static library
fat_manager = env.StaticLibrary('fat_manager', 'fat_manager.cc')

main = env.Object('main.o', ['main.cc'])

# link main.cpp with fat_manager
env.Program('fat', [main, fat_manager])
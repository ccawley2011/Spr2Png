cmake_minimum_required(VERSION 3.1...3.10)
project(Spr2Png LANGUAGES C)

add_compile_options(-O3 -Wall -Wextra -pedantic)

find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)

add_executable(png2spr
	png2spr.c
	p2s_ver.c
	library.c
	library.h
	types.h
)
target_link_libraries(png2spr PRIVATE ZLIB::ZLIB PNG::PNG)

cmake_minimum_required(VERSION 3.1...3.10)
project(Spr2Png LANGUAGES C)

add_compile_options(-O3 -Wall -Wextra -pedantic)

find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)

add_executable(spr2png
	spr2png.c
	s2p_const.c
	s2p_const.h
	s2p_frqsrt.c
	s2p_lib.h
	s2p_reduce.c
	s2p_trim.c
	s2p_ver.c
	library.c
	library.h
	types.h
)
target_link_libraries(spr2png PRIVATE ZLIB::ZLIB PNG::PNG)

add_executable(png2spr
	png2spr.c
	p2s_ver.c
	library.c
	library.h
	types.h
)
target_link_libraries(png2spr PRIVATE ZLIB::ZLIB PNG::PNG)

cmake_minimum_required(VERSION 3.1)
project(nexus_lib)

set (CMAKE_CXX_STANDARD 11)


SET(SOURCES
	src/common/cone.cpp
	src/common/frustum.cpp
	src/common/nexusdata.cpp
	src/nxszip/abitstream.cpp
	src/nxszip/atunstall.cpp
	src/nxszip/meshdecoder.cpp
)

include_directories(vcglib vcglib/eigenlib corto/include/)

add_library(nxs_commons STATIC ${SOURCES})

target_link_libraries(nxs_commons corto)
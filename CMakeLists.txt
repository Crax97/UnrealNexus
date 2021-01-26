#export CMAKE_PREFIX_PATH=path to Qt/5.5/gcc_64/ (where you would find bin/qmake)
cmake_minimum_required(VERSION 3.1)
project(nexus)

set (CMAKE_CXX_STANDARD 11)

set(SOURCES 
	../vcglib/wrap/gcache/token.h
	../vcglib/wrap/gcache/provider.h
	../vcglib/wrap/gcache/door.h
	../vcglib/wrap/gcache/dheap.h
	../vcglib/wrap/gcache/controller.h
	../vcglib/wrap/gcache/cache.h
    	./src/common/cone.h
    	./src/common/dag.h
    	./src/common/frustum.h
    	./src/common/nexusfile.h
	./src/common/nexusdata.h
    	./src/common/signature.h
    	./src/common/token.h

	./src/common/cone.cpp
	./src/common/frustum.cpp
	./src/common/nexusdata.cpp
	./src/nxszip/abitstream.cpp
	./src/nxszip/atunstall.cpp
	./src/nxszip/meshdecoder.cpp
)

include_directories(../vcglib ../vcglib/eigenlib ../corto/include)

add_library(nxs_commons ${SOURCES})

target_link_libraries(nxs_commons corto)

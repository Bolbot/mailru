cmake_minimum_required(VERSION 2.8)

project(Final C)

set(TARGET final)
set(AIOVER "USELIBEV")
set(AIOLIBFLAGS "-lev")
set(VERBOSE 1)
set(VERBOSEFLAGS "-DVERBOSE=${VERBOSE} -DVERBOSE_UTILS=0 -DAIOVER=${AIOVER} -D${AIOVER}")

set(REBUILDABLES "${TARGET} *.o")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ggdb -O0 ${VERBOSEFLAGS} ${AIOLIBFLAGS}")

add_library(server server.c)

add_library(utils utils.c)
add_executable(${TARGET} main.c)

target_link_libraries(${TARGET} utils server ${AIOLIBFLAGS})

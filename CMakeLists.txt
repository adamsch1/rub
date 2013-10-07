cmake_minimum_required (VERSION 2.6)
project (Rub)

set (Rub_VERSION_MAJOR_1)
set (Rub_VERSION_MINOR_0)

configure_file (
  "${PROJECT_SOURCE_DIR}/rub.h.in"
  "${PROJECT_SOURCE_DIR}/rub.h"
)

include_directories(/usr/local/include)
LINK_DIRECTORIES( /usr/local/lib )
SET(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} /usr/local/lib)

set(TCC tcc)
set(CMAKE_BUILD_TYPE Debug)
#SET(P_SOURCE http-server.c route.c)
SET(P_SOURCE rub.c route.c bt.c)

include_directories("${PROJECT_BINARY_DIR}")

add_executable(rub ${P_SOURCE} http-parser/http_parser.o)

TARGET_LINK_LIBRARIES(rub event libtcc.a dl uv)


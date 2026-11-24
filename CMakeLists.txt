cmake_minimum_required(VERSION 3.16)

project(fcitx5-mokz VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Fcitx5Core REQUIRED)

file(GLOB_RECURSE MARISA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/marisa/lib/marisa/*.cc"
)
add_library(marisa_static STATIC ${MARISA_SOURCES})
target_include_directories(marisa_static PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/marisa/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/marisa/lib"
)
target_compile_options(marisa_static PRIVATE -O3 -march=x86-64-v2 -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.)

add_library(mokz SHARED
    src/mokz.cpp
    third_party/hp-ime/reranker_engine.cpp
)

target_include_directories(mokz PRIVATE
    src
    src/input
    src/nlp
    src/storage
    src/ui
    third_party/hp-ime/include
)

target_link_libraries(mokz PRIVATE
    Fcitx5::Core
    marisa_static
)

target_compile_options(mokz PRIVATE -O3 -march=x86-64-v2 -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.)

if(DEFINED FCITX5_INSTALL_ADDON_DIR)
    set(ADDON_INSTALL_DIR ${FCITX5_INSTALL_ADDON_DIR})
else()
    set(ADDON_INSTALL_DIR lib/fcitx5)
endif()

install(TARGETS mokz LIBRARY DESTINATION ${ADDON_INSTALL_DIR})
install(FILES packaging/conf/addon/mokz.conf DESTINATION share/fcitx5/addon)
install(FILES packaging/conf/inputmethod/mokz.conf DESTINATION share/fcitx5/inputmethod)

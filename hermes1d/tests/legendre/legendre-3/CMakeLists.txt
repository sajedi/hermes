project(legendre-3)

add_executable(${PROJECT_NAME} main.cpp)
include (../../../CMake.common)

set(BIN ${PROJECT_BINARY_DIR}/${PROJECT_NAME})
add_test(legendre-3 ${BIN})
set_tests_properties(legendre-3 PROPERTIES COST -15)
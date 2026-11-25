add_library(mst_warnings INTERFACE)
target_compile_options(mst_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)

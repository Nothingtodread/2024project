# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: Vincent Pinon <vincent.pinon@asygn.com>

include_directories(${MLT_INCLUDE_DIR})
kde_enable_exceptions()
add_executable(fuzz main_fuzzer.cpp fuzzing.cpp)
add_executable(fuzz_reproduce main_reproducer.cpp fuzzing.cpp)
target_link_libraries(fuzz kdenliveLib -fsanitize=fuzzer)
target_link_libraries(fuzz_reproduce kdenliveLib)
set_property(TARGET fuzz PROPERTY CXX_STANDARD 14)
set_property(TARGET fuzz_reproduce PROPERTY CXX_STANDARD 14)

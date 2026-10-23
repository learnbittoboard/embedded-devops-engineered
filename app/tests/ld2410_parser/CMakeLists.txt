# Unit tests for the LD2410 frame parser — a plain Zephyr app whose
# only job is to host the ztest suite. The parser under test is the
# product's own file, compiled straight from app/src: the tests
# exercise the shipped code, never a copy of it.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ld2410_parser_test)

target_sources(app PRIVATE src/main.c ../../src/ld2410_parser.c)
target_include_directories(app PRIVATE ../../src)

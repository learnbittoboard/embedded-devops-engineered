# Chapter 9, Step 3 — The good v2: same 1.1.0 as Step 2, plus the one
# missing line — the image confirms itself once it has proven it works.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ch10_ota_package)

target_sources(app PRIVATE src/main.c src/ld2410.c)
target_include_directories(app PRIVATE include)

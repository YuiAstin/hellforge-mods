# One-Handed Test as a CMake project (for IDEs). HellForge builds it by itself from src/ before each session.
cmake_minimum_required(VERSION 3.25)
project(one_hand_test C)
include(../../app/sdk/cmake/HellForgeMod.cmake)

hellforge_add_mod(one_hand_test src/one_hand_test.c)

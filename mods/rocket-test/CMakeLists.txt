# Rocket Sentry Test as a CMake project (for IDEs). HellForge builds it by itself from src/ before each session.
cmake_minimum_required(VERSION 3.25)
project(rocket_test C)
include(../../app/sdk/cmake/HellForgeMod.cmake)

hellforge_add_mod(rocket_test src/rocket_test.c)

# Koleda as a CMake project (for IDEs). HellForge builds it by itself from src/ before each session.
cmake_minimum_required(VERSION 3.25)
project(koleda C)
include(../../app/sdk/cmake/HellForgeMod.cmake)

hellforge_add_mod(koleda src/koleda.c)

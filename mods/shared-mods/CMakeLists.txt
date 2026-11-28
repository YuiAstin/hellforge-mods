# Shared Mods as a CMake project (for IDEs). HellForge builds it by itself from src/ before each session.
cmake_minimum_required(VERSION 3.25)
project(shared_mods C)
include(../../app/sdk/cmake/HellForgeMod.cmake)

hellforge_add_mod(shared_mods src/shared_mods.c)

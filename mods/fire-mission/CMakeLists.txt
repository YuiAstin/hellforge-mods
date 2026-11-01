# Fire Mission as a CMake project (for IDEs). HellForge builds it by itself from src/ before each session.
cmake_minimum_required(VERSION 3.25)
project(fire_mission C)
include(../../app/sdk/cmake/HellForgeMod.cmake)

hellforge_add_mod(fire_mission src/fire_mission.c)

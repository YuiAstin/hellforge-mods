# Mech Probe as a CMake project (for IDEs). HellForge builds it by itself from src/ before each session.
cmake_minimum_required(VERSION 3.25)
project(mech_probe C)
include(../../app/sdk/cmake/HellForgeMod.cmake)

hellforge_add_mod(mech_probe src/mech_probe.c)

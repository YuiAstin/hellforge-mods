# Stim Probe as a CMake project (for IDEs). HellForge builds it by itself from src/ before each session.
cmake_minimum_required(VERSION 3.25)
project(stim_probe C)
include(../../app/sdk/cmake/HellForgeMod.cmake)

hellforge_add_mod(stim_probe src/stim_probe.c)

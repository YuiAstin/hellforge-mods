# Primed Sure Footed as a CMake project (for IDEs). HellForge builds it by itself from src/ before each session.
cmake_minimum_required(VERSION 3.25)
project(primed_sure_footed C)
include(../../app/sdk/cmake/HellForgeMod.cmake)

hellforge_add_mod(primed_sure_footed src/sure_footed.c)

# Voymastina: AK-15 on foot as a CMake project (for IDEs). HellForge builds it by itself from src/ before each session.
cmake_minimum_required(VERSION 3.25)
project(voymastina_ak15 C)
include(../../app/sdk/cmake/HellForgeMod.cmake)

hellforge_add_mod(voymastina_ak15 src/voymastina_ak15.c)

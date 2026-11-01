# Field Promotions as a CMake project (for IDEs). HellForge builds it by itself from src/ before each session.
cmake_minimum_required(VERSION 3.25)
project(field_promotions C)
include(../../app/sdk/cmake/HellForgeMod.cmake)

hellforge_add_mod(field_promotions src/field_promotions.c src/gas_doctrine.c src/gatling.c src/mg_sentry.c src/laser_sentry.c src/maxigun.c src/rocket_sentry.c)

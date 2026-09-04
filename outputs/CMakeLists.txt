cmake_minimum_required(VERSION 3.20)
project(NextBellCountdown LANGUAGES CXX)

add_executable(NextBellCountdown WIN32 NextBellCountdown.cpp)
target_compile_features(NextBellCountdown PRIVATE cxx_std_17)

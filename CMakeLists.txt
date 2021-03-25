cmake_minimum_required(VERSION 3.6)
project(discordpp-rest-beast)

set(CMAKE_CXX_STANDARD 14)

###################

add_library(${PROJECT_NAME} INTERFACE)

add_library(beast INTERFACE)
target_include_directories(beast INTERFACE ${DPP_Beast_SOURCE_DIR})

target_include_directories(${PROJECT_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} INTERFACE discordpp)

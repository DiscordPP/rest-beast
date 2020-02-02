cmake_minimum_required(VERSION 3.6)
project(discordpp-rest-beast)

set(CMAKE_CXX_STANDARD 14)

###################

add_library(${PROJECT_NAME} INTERFACE)

add_library(beast INTERFACE)
TARGET_INCLUDE_DIRECTORIES(beast INTERFACE ${DPP_Beast_SOURCE_DIR})

INCLUDE_DIRECTORIES(${discordpp_SOURCE_DIR} ${json_SOURCE_DIR})
TARGET_LINK_LIBRARIES(${PROJECT_NAME} INTERFACE)
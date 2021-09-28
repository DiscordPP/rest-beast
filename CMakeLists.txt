cmake_minimum_required(VERSION 3.15)
project(discordpp-rest-beast)

set(CMAKE_CXX_STANDARD 14)

###################

list(PREPEND ALL_DISCORDPP_PLUGINS discordpp::RestBeast)
list(APPEND ALL_DISCORDPP_LIBRARIES discordpp-rest-beast)
set(ALL_DISCORDPP_LIBRARIES ${ALL_DISCORDPP_LIBRARIES} PARENT_SCOPE)
set(ALL_DISCORDPP_PLUGINS ${ALL_DISCORDPP_PLUGINS} PARENT_SCOPE)

add_library(${PROJECT_NAME} INTERFACE)

add_library(beast INTERFACE)
target_include_directories(beast INTERFACE ${DPP_Beast_SOURCE_DIR})

target_include_directories(${PROJECT_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} INTERFACE discordpp)

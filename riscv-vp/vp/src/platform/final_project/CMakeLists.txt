file(GLOB_RECURSE HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_library(platform-final_project
ethernet.cpp
display.cpp
${HEADERS})

target_include_directories(platform-final_project PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(platform-final_project systemc)

add_executable(final_project
        main.cpp)

target_link_libraries(final_project rv32 platform-final_project platform-common gdb-mc ${Boost_LIBRARIES} systemc pthread)

INSTALL(TARGETS final_project RUNTIME DESTINATION bin)

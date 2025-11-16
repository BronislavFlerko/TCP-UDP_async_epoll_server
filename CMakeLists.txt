cmake_minimum_required(VERSION 3.5)

project(TCP-UDP_epoll_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(TCP-UDP_epoll_server main.cpp
    server.cpp

)

include(GNUInstallDirs)
install(TARGETS TCP-UDP_epoll_server
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

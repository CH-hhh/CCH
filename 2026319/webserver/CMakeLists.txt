cmake_minimum_required(VERSION 3.23)
project(webserver)

set(CMAKE_CXX_STANDARD 14)

add_executable(webserver src/main.c src/request.c src/response.c src/server.c src/util.c)

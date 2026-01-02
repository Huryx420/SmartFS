cmake_minimum_required(VERSION 3.10)
project(SmartFS)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra") # 开启警告，减少Bug

# 1. 查找依赖库
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)
pkg_check_modules(LIBS REQUIRED libzstd liblz4 libssl) # 预留给压缩和加密库

# 2. 设置头文件路径
include_directories(
    ${FUSE3_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/include
)

# 3. 自动查找 src 目录下的所有 .c 文件
# (注意：正式开发时通常建议手动列出，但初期为了方便先自动查找)
file(GLOB_RECURSE SOURCES "src/*.c")

# 4. 生成可执行文件
add_executable(smartfs ${SOURCES})

# 5. 链接库文件
target_link_libraries(smartfs 
    ${FUSE3_LIBRARIES}
    ${LIBS_LIBRARIES}
    m # 数学库
    pthread # 线程库
)

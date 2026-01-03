cmake_minimum_required(VERSION 3.10)
project(SmartFS)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# 1. 查找依赖库
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)
# pkg_check_modules(LIBS REQUIRED libzstd liblz4 libssl) # 暂时注释掉，用到再开

# 2. 【关键修复】设置正确的头文件路径
# 我们添加了 ${PROJECT_SOURCE_DIR}/src，这样 main.c 里的 #include "versioning/..." 才能找到文件
include_directories(
    ${FUSE3_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/src/include
    ${PROJECT_SOURCE_DIR}/src
)

# 3. 定义主程序 smartfs
# 【修正】必须把 versioning 目录下的 .c 文件也加进来，否则会报错“未定义的引用”
add_executable(smartfs 
    src/main.c
    src/versioning/version_mgr.c
    src/versioning/version_utils.c
)

target_link_libraries(smartfs 
    ${FUSE3_LIBRARIES}
    # ${LIBS_LIBRARIES}
    m
    pthread
)

# 4. 定义格式化工具 mkfs
# mkfs 通常只需要基础头文件，如果以后 mkfs 也要写版本信息，可能也需要加 version_mgr.c
add_executable(mkfs src/utils/mkfs.c)
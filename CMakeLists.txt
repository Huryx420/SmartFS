cmake_minimum_required(VERSION 3.10)
project(SmartFS)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# 1. 查找依赖库
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)
# pkg_check_modules(LIBS REQUIRED libzstd liblz4 libssl) # 暂时注释掉，用到再开

# 2. 【关键修复】设置正确的头文件路径
# 指向 src/include 而不是 include
include_directories(
    ${FUSE3_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/src/include
)

# 3. 定义主程序 smartfs (排除 mkfs.c)
# 注意：这里我们手动指定 main.c，防止把 mkfs.c 混进去
add_executable(smartfs src/main.c)

target_link_libraries(smartfs 
    ${FUSE3_LIBRARIES}
    # ${LIBS_LIBRARIES}
    m
    pthread
)

# 4. 定义格式化工具 mkfs
add_executable(mkfs src/utils/mkfs.c)
# mkfs 不需要链接 FUSE 库，只需要基础的 C 库即可

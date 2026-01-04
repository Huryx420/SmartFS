cmake_minimum_required(VERSION 3.10)
project(smartfs)

# 1. 设置标准和编译选项
set(CMAKE_C_STANDARD 99)
# -Wno-deprecated-declarations: 屏蔽 OpenSSL 3.0 的过时警告
# -Wno-unused-parameter: 屏蔽未使用的参数警告
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FILE_OFFSET_BITS=64 -g -Wno-deprecated-declarations -Wno-unused-parameter")

# 2. 查找库
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)
find_package(OpenSSL REQUIRED)

# 3. 包含头文件
include_directories(
    src/include
    src/versioning
    src/storage
    ${FUSE3_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

# ---------------------------------------------------------
# 目标 1: SmartFS 主程序
# ---------------------------------------------------------
add_executable(smartfs
    src/main.c
    src/versioning/version_mgr.c
    src/versioning/version_utils.c
    src/storage/l3_storage.c
    src/storage/cache.c
    src/storage/compress.c
    src/storage/dedup.c
    src/storage/smart_write.c
    src/storage/backup.c
    src/storage/wal.c
)

# 链接库 (强制直接链接 lz4)
target_link_libraries(smartfs 
    ${FUSE3_LIBRARIES} 
    OpenSSL::Crypto 
    lz4            # <--- 直接写库名，通常比 find_library 更稳
    pthread
)

# ---------------------------------------------------------
# 目标 2: mkfs 格式化工具
# ---------------------------------------------------------
add_executable(mkfs 
    src/utils/mkfs.c
)
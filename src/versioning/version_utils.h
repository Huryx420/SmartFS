#ifndef VERSION_UTILS_H
#define VERSION_UTILS_H

// 解析结果类型
typedef enum {
    VER_QUERY_NONE = 0,    // 普通文件
    VER_QUERY_ID,          // 指定版本号 (v1, v2)
    VER_QUERY_TIME         // 指定时间 (2h, yesterday)
} version_query_type_t;

// [修改] 函数原型更新：增加 query_str 用于返回时间字符串
version_query_type_t parse_version_path(const char *path, char *real_path, int *version_id, char *query_str);

#endif
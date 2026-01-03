#ifndef VERSION_UTILS_H
#define VERSION_UTILS_H

/**
 * 解析带有版本号的路径
 * * 示例输入: "/home/docs/report.txt@v3"
 * 输出:
 * real_path: "/home/docs/report.txt"
 * version_id: 3
 * * 返回值: 
 * 0: 普通文件 (无版本号)
 * 1: 历史版本 (有 @vN)
 * -1: 解析错误
 */
int parse_version_path(const char *path, char *real_path, int *version_id);

#endif
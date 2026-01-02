#include <stdio.h>
#include <string.h>
#include <openssl/sha.h> 

// 核心算法：输入任意数据，输出它的唯一指纹
void calculate_sha256(const char *input, size_t len, char *output) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;

    SHA256_Init(&sha256);
    SHA256_Update(&sha256, input, len);
    SHA256_Final(hash, &sha256);

    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[64] = 0; 
}

int main() {
    // 测试一下功能
    const char *data = "SmartFS Storage Engine Test";
    char result[65];

    calculate_sha256(data, strlen(data), result);

    printf("原始数据: %s\n", data);
    printf("数据指纹: %s\n", result);
    return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sys_monitor.h"

/**
 * @brief 读取 /proc/* 文件组装系统硬 件状态 JSON 字符串。
 * @param json_buffer 外部提供的输出缓 冲区
 * @param max_len     缓冲区最大容量
 */
void get_system_monitor_json(char *json_buffer, size_t max_len) {
    char line[256];
    FILE *fp;

    // 1. CPU
    int cores = 0;
    char cpu_model[128] = "Unknown";
    float cpu_mhz = 0.0;
    fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "processor", 9) == 0) cores++;
            if (strncmp(line, "model name", 10) == 0) {
                char *colon = strchr(line, ':');
                if (colon) {
                    strncpy(cpu_model, colon + 2, sizeof(cpu_model)-1);
                    cpu_model[strcspn(cpu_model, "\n")] = 0;
                }
            }
            if (strncmp(line, "cpu MHz", 7) == 0) {
                char *colon = strchr(line, ':');
                if (colon) cpu_mhz = atof(colon + 1);
            }
        }
        fclose(fp);
    }
    // 防止 JSON 转义错误，清除双引号等符号
    for (int i=0; cpu_model[i]; i++) {
        if (cpu_model[i] == '"' || cpu_model[i] == '\\' || cpu_model[i] == '\n') cpu_model[i] = ' ';
    }

    // 2. 内存
    long mem_total = 0, mem_free = 0, mem_avail = 0;
    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "MemTotal:", 9) == 0) sscanf(line + 9, "%ld", &mem_total);
            if (strncmp(line, "MemFree:", 8) == 0) sscanf(line + 8, "%ld", &mem_free);
            if (strncmp(line, "MemAvailable:", 13) == 0) sscanf(line + 13, "%ld", &mem_avail);
        }
        fclose(fp);
    }
    long mem_used = mem_total - (mem_avail > 0 ? mem_avail : mem_free);

    // 3. 网络 (累加所有网卡rx/tx除了lo)
    long long rx_bytes = 0, tx_bytes = 0;
    fp = fopen("/proc/net/dev", "r");
    if (fp) {
        fgets(line, sizeof(line), fp); // skip header 1
        fgets(line, sizeof(line), fp); // skip header 2
        while (fgets(line, sizeof(line), fp)) {
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = ' '; // replace ':' for easy parsing
                char iface[32] = {0};
                long long rx=0, tx=0, d[7];
                if (sscanf(line, "%s %lld %lld %lld %lld %lld %lld %lld %lld %lld",
                           iface, &rx, &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6], &tx) >= 10) {
                    if (strcmp(iface, "lo") != 0) {
                        rx_bytes += rx;
                        tx_bytes += tx;
                    }
                }
            }
        }
        fclose(fp);
    }

    // 4. 系统运行时间和负载
    float uptime = 0;
    fp = fopen("/proc/uptime", "r");
    if (fp) {
        fscanf(fp, "%f", &uptime);
        fclose(fp);
    }

    float load1=0, load5=0, load15=0;
    fp = fopen("/proc/loadavg", "r");
    if (fp) {
        fscanf(fp, "%f %f %f", &load1, &load5, &load15);
        fclose(fp);
    }

    // 拼接 JSON 输出
    snprintf(json_buffer, max_len,
        "{"
        "\"cpu\":{\"model\":\"%s\",\"cores\":%d,\"mhz\":%.2f},"
        "\"mem\":{\"total\":%ld,\"used\":%ld},"
        "\"net\":{\"rx\":%lld,\"tx\":%lld},"
        "\"sys\":{\"uptime\":%.0f,\"load\":[%.2f,%.2f,%.2f]}"
        "}",
        cpu_model, cores, cpu_mhz,
        mem_total, mem_used,
        rx_bytes, tx_bytes,
        uptime, load1, load5, load15);
}

/* resource_daemon.c — системний демон моніторингу ресурсів Unix/Linux */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#define LOG_FILE "/var/log/resource_monitor.log"
#define MAX_LOG_SIZE (1024 * 1024) /* 1 МБ для ротації */
#define INTERVAL_SEC 10

static volatile sig_atomic_t keep_running = 1;
void handle_sigterm(int sig)
{
    (void)sig;
    keep_running = 0;
}

void check_and_rotate_log(void)
{
    struct stat st;
    if (stat(LOG_FILE, &st) == 0) {
        if (st.st_size > MAX_LOG_SIZE) {
            char backup_file[256];
            snprintf(backup_file, sizeof(backup_file), "%s.old", LOG_FILE);
            rename(LOG_FILE, backup_file);
        }
    }
}

void write_log(const char *message)
{
    check_and_rotate_log();
    int fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) return;

    time_t now = time(NULL);
    char *time_str = ctime(&now);
    time_str[strcspn(time_str, "\n")] = 0; /* прибрати символ нового рядка */

    char log_buf[512];
    int len = snprintf(log_buf, sizeof(log_buf), "[%s] %s\n", time_str, message);
    
    write(fd, log_buf, len);
    close(fd);
}

void get_memory_info(char *buf, size_t size)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) {
        snprintf(buf, size, "Memory: не вдалося прочитати /proc/meminfo");
        return;
    }

    unsigned long total = 0, free_mem = 0, available = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %lu kB", &total) == 1) continue;
        if (sscanf(line, "MemFree: %lu kB", &free_mem) == 1) continue;
        if (sscanf(line, "MemAvailable: %lu kB", &available) == 1) continue;
    }
    fclose(f);

    snprintf(buf, size, "Memory Total: %lu MB, Available: %lu MB", total / 1024, available / 1024);
}

void get_disk_info(char *buf, size_t size)
{
    struct statvfs stat;
    if (statvfs("/", &stat) != 0) {
        snprintf(buf, size, "Disk: помилка отримання статистики");
        return;
    }

    unsigned long long total_mb = (stat.f_blocks * stat.f_frsize) / (1024 * 1024);
    unsigned long long free_mb = (stat.f_bfree * stat.f_frsize) / (1024 * 1024);

    snprintf(buf, size, "Disk Root Total: %llu MB, Free: %llu MB", total_mb, free_mb);
}

void daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS); /* Вихід батьківського процесу */

    if (setsid() < 0) exit(EXIT_FAILURE);

    signal(SIGHUP, SIG_IGN);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);
    chdir("/");

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);
}

int main(void)
{
    daemonize();

    /* Реєстрація обробника сигналу SIGTERM */
    struct sigaction sa;
    sa.sa_handler = handle_sigterm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);

    write_log("Демон моніторингу ресурсів успішно запущено.");

    while (keep_running) {
        char mem_buf[128];
        char disk_buf[128];
        char full_msg[300];

        get_memory_info(mem_buf, sizeof(mem_buf));
        get_disk_info(disk_buf, sizeof(disk_buf));

        snprintf(full_msg, sizeof(full_msg), "%s | %s", mem_buf, disk_buf);
        write_log(full_msg);

        sleep(INTERVAL_SEC);
    }

    write_log("Демон зупинено за сигналом SIGTERM.");
    return EXIT_SUCCESS;
}

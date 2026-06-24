#include "epaper.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <inttypes.h>
#include <limits.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wiringPi.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static uint8_t image_bw[50 * 300];

typedef struct
{
    char hostname[64];
    char user[32];
    char now[32];
    char date[32];
    char usb_ip[INET_ADDRSTRLEN];
    char wlan_ip[INET_ADDRSTRLEN];
    char uptime[32];
    char rssi[24];
    char cpu_temp[24];
    char load_avg[24];
    char ram_free[24];
    char disk_free[24];
    char sd_status[24];
    char ssh_status[24];
    char epd_status[24];
} Status;

typedef struct
{
    bool loaded;
    uint64_t hash;
    int fast_count;
} DisplayState;

typedef enum
{
    REFRESH_AUTO,
    REFRESH_FULL,
    REFRESH_FAST
} RefreshMode;

typedef struct
{
    const char *page;
    const char *state_path;
    RefreshMode refresh_mode;
    bool force;
    int full_every;
} Options;

static void cleanup(int signo)
{
    (void)signo;
    epd_io_deinit();
    exit(0);
}

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0)
        return;

    snprintf(dst, dst_size, "%s", src ? src : "");
}

static int round_nearest_step(int value, int step)
{
    if (step <= 1)
        return value;

    if (value >= 0)
        return ((value + step / 2) / step) * step;

    return -(((-value + step / 2) / step) * step);
}

static int floor_step(int value, int step)
{
    if (step <= 1)
        return value;

    return (value / step) * step;
}

static void read_hostname(Status *status)
{
    if (gethostname(status->hostname, sizeof(status->hostname)) != 0)
        copy_text(status->hostname, sizeof(status->hostname), "zero2");

    status->hostname[sizeof(status->hostname) - 1] = '\0';
}

static void read_user(Status *status)
{
    const char *env_user = getenv("USER");
    struct passwd *pw = getpwuid(geteuid());

    if (pw && pw->pw_name)
        copy_text(status->user, sizeof(status->user), pw->pw_name);
    else if (env_user)
        copy_text(status->user, sizeof(status->user), env_user);
    else
        copy_text(status->user, sizeof(status->user), "michal");
}

static void read_time(Status *status)
{
    time_t now = time(NULL);
    struct tm tm_now;

    if (localtime_r(&now, &tm_now) == NULL)
    {
        copy_text(status->now, sizeof(status->now), "time unknown");
        copy_text(status->date, sizeof(status->date), "date unknown");
        return;
    }

    tm_now.tm_min = (tm_now.tm_min / 5) * 5;
    tm_now.tm_sec = 0;

    strftime(status->now, sizeof(status->now), "%H:%M", &tm_now);
    strftime(status->date, sizeof(status->date), "%Y-%m-%d", &tm_now);
}

static void read_uptime(Status *status)
{
    FILE *file = fopen("/proc/uptime", "r");
    double seconds = 0.0;

    if (!file)
    {
        copy_text(status->uptime, sizeof(status->uptime), "uptime n/a");
        return;
    }

    if (fscanf(file, "%lf", &seconds) != 1)
    {
        fclose(file);
        copy_text(status->uptime, sizeof(status->uptime), "uptime n/a");
        return;
    }

    fclose(file);

    int total_minutes = floor_step((int)(seconds / 60.0), 10);
    int days = total_minutes / (24 * 60);
    int hours = (total_minutes / 60) % 24;
    int minutes = total_minutes % 60;

    if (days > 0)
        snprintf(status->uptime, sizeof(status->uptime), "up %dd %02dh", days, hours);
    else
        snprintf(status->uptime, sizeof(status->uptime), "up %02dh %02dm", hours, minutes);
}

static void save_ip_for_iface(Status *status, const char *iface, const char *ip)
{
    if (strcmp(iface, "usb0") == 0)
        copy_text(status->usb_ip, sizeof(status->usb_ip), ip);
    else if (strcmp(iface, "wlan0") == 0)
        copy_text(status->wlan_ip, sizeof(status->wlan_ip), ip);
}

static void read_network(Status *status)
{
    struct ifaddrs *ifaddr = NULL;

    if (getifaddrs(&ifaddr) != 0)
        return;

    for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        char ip[INET_ADDRSTRLEN];
        struct sockaddr_in *addr;

        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;

        if ((ifa->ifa_flags & IFF_LOOPBACK) != 0)
            continue;

        addr = (struct sockaddr_in *)ifa->ifa_addr;
        if (!inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip)))
            continue;

        save_ip_for_iface(status, ifa->ifa_name, ip);
    }

    freeifaddrs(ifaddr);
}

static void read_rssi(Status *status)
{
    FILE *file = fopen("/proc/net/wireless", "r");
    char buffer[256];

    copy_text(status->rssi, sizeof(status->rssi), "rssi -");

    if (!file)
        return;

    while (fgets(buffer, sizeof(buffer), file))
    {
        double level = 0.0;

        if (!strstr(buffer, "wlan0:"))
            continue;

        if (sscanf(buffer, " wlan0: %*s %*s %lf", &level) == 1)
        {
            if (level >= -65.0)
                copy_text(status->rssi, sizeof(status->rssi), "rssi good");
            else if (level >= -80.0)
                copy_text(status->rssi, sizeof(status->rssi), "rssi ok");
            else
                copy_text(status->rssi, sizeof(status->rssi), "rssi weak");
        }
        break;
    }

    fclose(file);
}

static void read_cpu_temp(Status *status)
{
    FILE *file = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    int milli_c = 0;

    copy_text(status->cpu_temp, sizeof(status->cpu_temp), "temp -");

    if (!file)
        return;

    if (fscanf(file, "%d", &milli_c) == 1)
    {
        int temp_c = round_nearest_step((milli_c + 500) / 1000, 5);
        snprintf(status->cpu_temp, sizeof(status->cpu_temp), "temp %dC", temp_c);
    }

    fclose(file);
}

static void read_load_avg(Status *status)
{
    FILE *file = fopen("/proc/loadavg", "r");
    double load = 0.0;

    copy_text(status->load_avg, sizeof(status->load_avg), "load -");

    if (!file)
        return;

    if (fscanf(file, "%lf", &load) == 1)
    {
        int bucket = (int)(load * 5.0 + 0.5);
        snprintf(status->load_avg, sizeof(status->load_avg), "load %.1f", bucket / 5.0);
    }

    fclose(file);
}

static void read_ram_free(Status *status)
{
    FILE *file = fopen("/proc/meminfo", "r");
    char key[64];
    char unit[16];
    int value_kb = 0;

    copy_text(status->ram_free, sizeof(status->ram_free), "ram -");

    if (!file)
        return;

    while (fscanf(file, "%63s %d %15s", key, &value_kb, unit) == 3)
    {
        if (strcmp(key, "MemAvailable:") == 0)
        {
            int free_mb = floor_step(value_kb / 1024, 50);
            snprintf(status->ram_free, sizeof(status->ram_free), "ram %dM", free_mb);
            break;
        }
    }

    fclose(file);
}

static int read_root_free_mb(void)
{
    struct statvfs fs;
    unsigned long long bytes;

    if (statvfs("/", &fs) != 0)
        return -1;

    bytes = (unsigned long long)fs.f_bavail * (unsigned long long)fs.f_frsize;
    return (int)(bytes / 1024ULL / 1024ULL);
}

static void read_disk_free(Status *status)
{
    int free_mb = read_root_free_mb();

    copy_text(status->disk_free, sizeof(status->disk_free), "disk -");
    copy_text(status->sd_status, sizeof(status->sd_status), "sd -");

    if (free_mb < 0)
        return;

    free_mb = floor_step(free_mb, 250);
    int free_deci_gb = (free_mb * 10) / 1024;
    snprintf(status->disk_free, sizeof(status->disk_free), "disk %d.%dG", free_deci_gb / 10, free_deci_gb % 10);
    copy_text(status->sd_status, sizeof(status->sd_status), free_mb < 1024 ? "sd low" : "sd ok");
}

static void read_services(Status *status)
{
    int ssh_ok = system("systemctl is-active --quiet ssh >/dev/null 2>&1") == 0;
    int epd_ok = system("systemctl is-active --quiet epd-status.timer >/dev/null 2>&1") == 0;

    copy_text(status->ssh_status, sizeof(status->ssh_status), ssh_ok ? "ssh ok" : "ssh fail");
    copy_text(status->epd_status, sizeof(status->epd_status), epd_ok ? "epd ok" : "epd off");
}

static Status collect_status(void)
{
    Status status;
    memset(&status, 0, sizeof(status));

    read_hostname(&status);
    read_user(&status);
    read_time(&status);
    read_uptime(&status);
    read_network(&status);
    read_rssi(&status);
    read_cpu_temp(&status);
    read_load_avg(&status);
    read_ram_free(&status);
    read_disk_free(&status);
    read_services(&status);

    return status;
}

static void line(uint16_t x, uint16_t y, uint16_t font, const char *fmt, ...)
{
    char text[96];
    va_list args;

    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);

    epd_paint_showString(x, y, (uint8_t *)text, font, EPD_COLOR_BLACK);
}

static const char *ip_or_dash(const char *ip)
{
    return ip && ip[0] ? ip : "-";
}

static uint64_t fnv1a64(const uint8_t *data, size_t length)
{
    uint64_t hash = 1469598103934665603ULL;

    for (size_t i = 0; i < length; i++)
    {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }

    return hash;
}

static uint64_t framebuffer_hash(void)
{
    return fnv1a64(image_bw, (size_t)EPD_H * (size_t)EPD_W_BUFF_SIZE);
}

static const char *default_state_path(void)
{
    static char path[PATH_MAX];
    const char *override = getenv("EPD_STATUS_STATE");
    const char *home = getenv("HOME");

    if (override && override[0])
        return override;

    if (home && home[0])
    {
        snprintf(path, sizeof(path), "%s/.cache/rp_pi2_zero/epd_status.state", home);
        return path;
    }

    return "/tmp/epd_290_bw_status.state";
}

static int ensure_parent_dirs(const char *path)
{
    char tmp[PATH_MAX];
    size_t length;

    if (strlen(path) >= sizeof(tmp))
        return -1;

    copy_text(tmp, sizeof(tmp), path);
    length = strlen(tmp);

    for (size_t i = 1; i < length; i++)
    {
        if (tmp[i] != '/')
            continue;

        tmp[i] = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
            return -1;
        tmp[i] = '/';
    }

    return 0;
}

static DisplayState read_display_state(const char *path)
{
    DisplayState state;
    FILE *file;

    memset(&state, 0, sizeof(state));
    file = fopen(path, "r");
    if (!file)
        return state;

    if (fscanf(file, "hash=%" SCNx64 "\nfast_count=%d", &state.hash, &state.fast_count) == 2)
    {
        if (state.fast_count < 0)
            state.fast_count = 0;
        state.loaded = true;
    }

    fclose(file);
    return state;
}

static void write_display_state(const char *path, uint64_t hash, int fast_count)
{
    char tmp_path[PATH_MAX];
    FILE *file;

    if (ensure_parent_dirs(path) != 0)
    {
        fprintf(stderr, "cannot create state directory for %s\n", path);
        return;
    }

    if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path) >= (int)sizeof(tmp_path))
    {
        fprintf(stderr, "state file path is too long: %s\n", path);
        return;
    }

    file = fopen(tmp_path, "w");
    if (!file)
    {
        fprintf(stderr, "cannot write state file %s\n", tmp_path);
        return;
    }

    fprintf(file, "hash=%016" PRIx64 "\nfast_count=%d\n", hash, fast_count);
    fclose(file);

    if (rename(tmp_path, path) != 0)
        fprintf(stderr, "cannot replace state file %s\n", path);
}

static void draw_status_page(const Status *status)
{
    epd_paint_clear(EPD_COLOR_WHITE);
    line(6, 0, EPD_FONT_SIZE12x6, "%s  %s  %s", status->now, status->date, status->uptime);
    line(6, 14, EPD_FONT_SIZE12x6, "user: %s", status->user);
    line(6, 28, EPD_FONT_SIZE12x6, "ssh: ssh %s@%s.local", status->user, status->hostname);
    line(6, 42, EPD_FONT_SIZE12x6, "usb0:  %s", ip_or_dash(status->usb_ip));
    line(6, 56, EPD_FONT_SIZE12x6, "wlan0: %s", ip_or_dash(status->wlan_ip));
    line(6, 70, EPD_FONT_SIZE12x6, "%s", status->rssi);
    line(150, 70, EPD_FONT_SIZE12x6, "%s", status->cpu_temp);
    line(6, 84, EPD_FONT_SIZE12x6, "%s", status->load_avg);
    line(150, 84, EPD_FONT_SIZE12x6, "%s", status->ram_free);
    line(6, 98, EPD_FONT_SIZE12x6, "%s", status->disk_free);
    line(150, 98, EPD_FONT_SIZE12x6, "%s", status->sd_status);
    line(6, 112, EPD_FONT_SIZE12x6, "%s", status->ssh_status);
    line(150, 112, EPD_FONT_SIZE12x6, "%s", status->epd_status);
}

static void draw_network_page(const Status *status)
{
    epd_paint_clear(EPD_COLOR_WHITE);
    line(6, 0, EPD_FONT_SIZE16x8, "SSH CONNECT");
    line(6, 20, EPD_FONT_SIZE12x6, "mDNS:");
    line(6, 34, EPD_FONT_SIZE12x6, "ssh %s@%s.local", status->user, status->hostname);
    line(6, 54, EPD_FONT_SIZE12x6, "USB:");
    line(6, 68, EPD_FONT_SIZE12x6, "ssh %s@%s", status->user, ip_or_dash(status->usb_ip));
    line(6, 88, EPD_FONT_SIZE12x6, "WIFI:");
    line(6, 102, EPD_FONT_SIZE12x6, "ssh %s@%s", status->user, ip_or_dash(status->wlan_ip));
}

static void draw_time_page(const Status *status)
{
    epd_paint_clear(EPD_COLOR_WHITE);
    line(20, 6, EPD_FONT_SIZE24x12, "%s", status->now);
    line(20, 36, EPD_FONT_SIZE16x8, "%s", status->date);
    line(20, 58, EPD_FONT_SIZE16x8, "%s", status->hostname);
    line(20, 82, EPD_FONT_SIZE12x6, "%s", status->uptime);
    line(20, 104, EPD_FONT_SIZE8x6, "use --fast for clock updates");
}

static void draw_help_page(const Status *status)
{
    (void)status;
    epd_paint_clear(EPD_COLOR_WHITE);
    line(6, 0, EPD_FONT_SIZE16x8, "EPD MENU");
    line(6, 22, EPD_FONT_SIZE12x6, "epd_290_bw_status status");
    line(6, 38, EPD_FONT_SIZE12x6, "epd_290_bw_status net");
    line(6, 54, EPD_FONT_SIZE12x6, "epd_290_bw_status time");
    line(6, 70, EPD_FONT_SIZE12x6, "epd_290_bw_status help");
    line(6, 94, EPD_FONT_SIZE8x6, "--full clean, slow");
    line(6, 106, EPD_FONT_SIZE8x6, "--fast quicker, may ghost");
}

static void draw_page(const char *page, const Status *status)
{
    if (strcmp(page, "net") == 0)
        draw_network_page(status);
    else if (strcmp(page, "time") == 0)
        draw_time_page(status);
    else if (strcmp(page, "help") == 0)
        draw_help_page(status);
    else
        draw_status_page(status);
}

static void show_page(bool fast_refresh)
{
    if (fast_refresh)
    {
        if (epd_init_fast())
            cleanup(0);
        epd_displayBW_fast(image_bw);
    }
    else
    {
        if (epd_init())
            cleanup(0);
        epd_displayBW(image_bw);
    }
}

static RefreshMode choose_refresh_mode(const Options *options, DisplayState state)
{
    if (options->refresh_mode != REFRESH_AUTO)
        return options->refresh_mode;

    if (!state.loaded)
        return REFRESH_FULL;

    if (options->full_every > 0 && state.fast_count >= options->full_every)
        return REFRESH_FULL;

    return REFRESH_FAST;
}

static Options parse_options(int argc, char **argv)
{
    Options options;
    options.page = "status";
    options.state_path = default_state_path();
    options.refresh_mode = REFRESH_AUTO;
    options.force = false;
    options.full_every = 5;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--auto") == 0)
            options.refresh_mode = REFRESH_AUTO;
        else if (strcmp(argv[i], "--fast") == 0)
            options.refresh_mode = REFRESH_FAST;
        else if (strcmp(argv[i], "--full") == 0)
            options.refresh_mode = REFRESH_FULL;
        else if (strcmp(argv[i], "--force") == 0)
            options.force = true;
        else if (strcmp(argv[i], "--state") == 0 && i + 1 < argc)
            options.state_path = argv[++i];
        else if (strcmp(argv[i], "--full-every") == 0 && i + 1 < argc)
            options.full_every = atoi(argv[++i]);
        else
            options.page = argv[i];
    }

    if (options.full_every < 0)
        options.full_every = 0;

    return options;
}

int main(int argc, char **argv)
{
    Options options = parse_options(argc, argv);
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    Status status = collect_status();
    DisplayState state;
    uint64_t hash;
    RefreshMode refresh_mode;
    int next_fast_count = 0;

    epd_set_panel(EPD213_219, 128, 296);
    epd_paint_newimage(image_bw, EPD_W, EPD_H, EPD_ROTATE_0, EPD_COLOR_WHITE);
    epd_paint_selectimage(image_bw);

    draw_page(options.page, &status);
    hash = framebuffer_hash();
    state = read_display_state(options.state_path);

    if (!options.force && state.loaded && state.hash == hash)
    {
        printf("e-paper status unchanged; skipping refresh\n");
        return 0;
    }

    refresh_mode = choose_refresh_mode(&options, state);
    next_fast_count = refresh_mode == REFRESH_FAST ? state.fast_count + 1 : 0;

    epd_io_init();
    show_page(refresh_mode == REFRESH_FAST);
    epd_enter_deepsleepmode(EPD_DEEPSLEEP_MODE1);
    epd_io_deinit();
    write_display_state(options.state_path, hash, next_fast_count);

    return 0;
}

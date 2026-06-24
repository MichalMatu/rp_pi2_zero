#include "epaper.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wiringPi.h>

static uint8_t image_bw[50 * 300];

static double monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void cleanup(int signo)
{
    (void)signo;
    epd_io_deinit();
    exit(0);
}

static int timed_init(const char *label, uint8_t (*fn)(void))
{
    double start = monotonic_ms();
    uint8_t rc = fn();
    double elapsed = monotonic_ms() - start;
    printf("%-26s %8.1f ms%s\n", label, elapsed, rc ? " ERROR" : "");
    return rc ? 1 : 0;
}

static void timed_display(const char *label, void (*fn)(uint8_t *), uint8_t *image)
{
    double start = monotonic_ms();
    fn(image);
    double elapsed = monotonic_ms() - start;
    double fps = elapsed > 0.0 ? 1000.0 / elapsed : 0.0;
    printf("%-26s %8.1f ms  %.2f FPS\n", label, elapsed, fps);
}

static void draw_screen(const char *mode, int counter)
{
    char line[32];

    epd_paint_clear(EPD_COLOR_WHITE);
    epd_paint_showString(8, 0, (uint8_t *)"2.9 BW", EPD_FONT_SIZE24x12, EPD_COLOR_BLACK);
    epd_paint_showString(8, 30, (uint8_t *)mode, EPD_FONT_SIZE24x12, EPD_COLOR_BLACK);

    snprintf(line, sizeof(line), "Num=%04d", counter);
    epd_paint_showString(8, 62, (uint8_t *)line, EPD_FONT_SIZE24x12, EPD_COLOR_BLACK);
    epd_paint_showString(8, 96, (uint8_t *)"BUSY GPIO24", EPD_FONT_SIZE16x8, EPD_COLOR_BLACK);
}

int main(int argc, char **argv)
{
    int partial_count = 10;

    if (argc > 1)
    {
        partial_count = atoi(argv[1]);
        if (partial_count < 0)
            partial_count = 0;
    }

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    epd_set_panel(EPD213_219, 128, 296);
    epd_io_init();
    epd_paint_newimage(image_bw, EPD_W, EPD_H, EPD_ROTATE_0, EPD_COLOR_WHITE);
    epd_paint_selectimage(image_bw);

    puts("WeAct 2.9 BW refresh benchmark");
    puts("Panel: 296x128, BUSY GPIO24 / physical pin 18");

    draw_screen("Full Refresh", 0);
    if (timed_init("epd_init", epd_init))
        cleanup(0);
    timed_display("epd_displayBW", epd_displayBW, image_bw);
    epd_enter_deepsleepmode(EPD_DEEPSLEEP_MODE1);
    delay(1000);

    draw_screen("Fast Refresh", 1);
    if (timed_init("epd_init_fast", epd_init_fast))
        cleanup(0);
    timed_display("epd_displayBW_fast", epd_displayBW_fast, image_bw);
    epd_enter_deepsleepmode(EPD_DEEPSLEEP_MODE1);
    delay(500);

    if (partial_count > 0)
    {
        if (timed_init("epd_init_partial", epd_init_partial))
            cleanup(0);

        for (int i = 1; i <= partial_count; i++)
        {
            draw_screen("Partial", i);
            timed_display("epd_displayBW_partial", epd_displayBW_partial, image_bw);
            delay(100);
        }

        epd_enter_deepsleepmode(EPD_DEEPSLEEP_MODE1);
        delay(500);
    }

    draw_screen("Cleanup Full", partial_count);
    if (timed_init("cleanup epd_init", epd_init))
        cleanup(0);
    timed_display("cleanup epd_displayBW", epd_displayBW, image_bw);
    epd_enter_deepsleepmode(EPD_DEEPSLEEP_MODE1);
    epd_io_deinit();

    return 0;
}

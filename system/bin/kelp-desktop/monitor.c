/*
 * kelp-desktop :: monitor.c
 * System monitor: /proc/kelp/* metrics, animated bar/line charts.
 *
 * SPDX-License-Identifier: MIT
 */

#include "monitor.h"
#include "desktop.h"
#include "render.h"
#include "theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>

/* ---- Metrics state ------------------------------------------------------ */

#define CPU_HISTORY_LEN  60

typedef struct {
    /* /proc/kelp/stats */
    long messages_processed;
    long bytes_read;
    long bytes_written;
    int  active_sessions;
    long uptime_sec;

    /* /proc/kelp/scheduler */
    int  queue_depth;
    long total_submitted;
    long total_completed;

    /* System */
    long mem_total_kb;
    long mem_free_kb;
    double load_avg;
    char kernel_version[64];

    /* CPU history (load average samples). */
    double cpu_history[CPU_HISTORY_LEN];
    int    cpu_history_idx;

    bool  kelp_available;
    uint32_t last_update_ms;
} monitor_state_t;

static monitor_state_t g_mon;

/* ---- Proc helpers ------------------------------------------------------- */

static void read_proc(const char *path, char *buf, size_t sz)
{
    buf[0] = '\0';
    FILE *f = fopen(path, "r");
    if (!f) return;
    size_t n = fread(buf, 1, sz - 1, f);
    buf[n] = '\0';
    fclose(f);
}

static long proc_val(const char *buf, const char *key)
{
    const char *p = strstr(buf, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    return strtol(p, NULL, 10);
}

static void refresh_metrics(void)
{
    char buf[2048];

    /* Kelp kernel module. */
    read_proc("/proc/kelp/stats", buf, sizeof(buf));
    if (buf[0]) {
        g_mon.kelp_available = true;
        g_mon.messages_processed = proc_val(buf, "messages_processed");
        g_mon.bytes_read = proc_val(buf, "bytes_read");
        g_mon.bytes_written = proc_val(buf, "bytes_written");
        g_mon.active_sessions = (int)proc_val(buf, "active_sessions");
        g_mon.uptime_sec = proc_val(buf, "uptime_seconds");
    }

    read_proc("/proc/kelp/scheduler", buf, sizeof(buf));
    if (buf[0]) {
        g_mon.queue_depth = (int)proc_val(buf, "queue_depth");
        g_mon.total_submitted = proc_val(buf, "total_submitted");
        g_mon.total_completed = proc_val(buf, "total_completed");
    }

    /* System memory. */
    read_proc("/proc/meminfo", buf, sizeof(buf));
    if (buf[0]) {
        g_mon.mem_total_kb = proc_val(buf, "MemTotal");
        g_mon.mem_free_kb = proc_val(buf, "MemAvailable");
        if (g_mon.mem_free_kb == 0)
            g_mon.mem_free_kb = proc_val(buf, "MemFree");
    }

    /* Load average. */
    read_proc("/proc/loadavg", buf, sizeof(buf));
    if (buf[0])
        g_mon.load_avg = strtod(buf, NULL);

    /* CPU history. */
    g_mon.cpu_history[g_mon.cpu_history_idx] = g_mon.load_avg;
    g_mon.cpu_history_idx = (g_mon.cpu_history_idx + 1) % CPU_HISTORY_LEN;

    /* Kernel version (once). */
    if (!g_mon.kernel_version[0]) {
        struct utsname uts;
        if (uname(&uts) == 0)
            snprintf(g_mon.kernel_version, sizeof(g_mon.kernel_version),
                     "%s", uts.release);
    }
}

/* ---- Init/Shutdown ------------------------------------------------------ */

void kd_monitor_init(kd_desktop_t *d)
{
    (void)d;
    memset(&g_mon, 0, sizeof(g_mon));
    refresh_metrics();
}

void kd_monitor_shutdown(kd_desktop_t *d)
{
    (void)d;
}

/* ---- Update ------------------------------------------------------------- */

void kd_monitor_update(kd_desktop_t *d, uint32_t now_ms)
{
    if (now_ms - g_mon.last_update_ms < 1000) return;
    g_mon.last_update_ms = now_ms;
    refresh_metrics();
    d->needs_redraw = true;
}

/* ---- Drawing ------------------------------------------------------------ */

static void draw_section_title(cairo_t *cr, const char *title,
                                double x, double *y)
{
    kd_draw_text_bold(cr, title, x, *y,
                       KD_FONT_FAMILY, KD_FONT_SIZE_NORMAL,
                       KD_ACCENT_GREEN, 0);
    *y += 22;
}

static void draw_metric_row(cairo_t *cr, const char *label, const char *value,
                              kd_color_t value_color, double x, double *y,
                              double w)
{
    kd_draw_text(cr, label, x, *y,
                  KD_FONT_FAMILY, KD_FONT_SIZE_SMALL,
                  KD_TEXT_SECONDARY, 0);

    int vw, vh;
    kd_measure_text(cr, value, KD_FONT_FAMILY, KD_FONT_SIZE_SMALL, &vw, &vh);
    kd_draw_text(cr, value, x + w - vw - 32, *y,
                  KD_FONT_FAMILY, KD_FONT_SIZE_SMALL,
                  value_color, 0);
    *y += 18;
}

static void draw_load_graph(cairo_t *cr, double x, double y,
                              double w, double h)
{
    /* Background. */
    kd_fill_rounded_rect(cr, x, y, w, h, 4.0, KD_BG_SURFACE);

    /* Grid lines. */
    for (int i = 1; i < 4; i++) {
        double gy = y + h * (double)i / 4.0;
        kd_draw_hline(cr, x, gy, w,
                       (kd_color_t){0.2, 0.2, 0.2, 0.3});
    }

    /* Line graph. */
    cairo_save(cr);
    cairo_set_source_rgba(cr, 0.0, 0.784, 0.325, 0.8);
    cairo_set_line_width(cr, 1.5);

    double max_val = 2.0; /* Assume max load of 2.0 for scaling. */
    for (int i = 0; i < CPU_HISTORY_LEN; i++) {
        if (g_mon.cpu_history[i] > max_val)
            max_val = g_mon.cpu_history[i] * 1.2;
    }

    bool first = true;
    for (int i = 0; i < CPU_HISTORY_LEN; i++) {
        int idx = (g_mon.cpu_history_idx + i) % CPU_HISTORY_LEN;
        double px = x + (w * (double)i / (CPU_HISTORY_LEN - 1));
        double val = g_mon.cpu_history[idx];
        double py = y + h - (val / max_val) * h;

        if (first) { cairo_move_to(cr, px, py); first = false; }
        else cairo_line_to(cr, px, py);
    }
    cairo_stroke(cr);

    /* Fill under the line. */
    cairo_set_source_rgba(cr, 0.0, 0.784, 0.325, 0.1);
    first = true;
    for (int i = 0; i < CPU_HISTORY_LEN; i++) {
        int idx = (g_mon.cpu_history_idx + i) % CPU_HISTORY_LEN;
        double px = x + (w * (double)i / (CPU_HISTORY_LEN - 1));
        double val = g_mon.cpu_history[idx];
        double py = y + h - (val / max_val) * h;

        if (first) { cairo_move_to(cr, px, py); first = false; }
        else cairo_line_to(cr, px, py);
    }
    cairo_line_to(cr, x + w, y + h);
    cairo_line_to(cr, x, y + h);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_restore(cr);
}

void kd_monitor_draw(kd_desktop_t *d, cairo_t *cr,
                      double x, double y, double w, double h)
{
    (void)d;

    double pad = KD_PANEL_PADDING;
    double mx = x + pad;
    double my = y + pad;
    double mw = w - pad * 2;

    /* Kelp kernel section. */
    draw_section_title(cr, "/dev/kelp", mx, &my);

    if (g_mon.kelp_available) {
        char val[32];
        snprintf(val, sizeof(val), "%ld", g_mon.messages_processed);
        draw_metric_row(cr, "Messages", val, KD_TEXT_PRIMARY, mx, &my, mw);

        snprintf(val, sizeof(val), "%d", g_mon.active_sessions);
        draw_metric_row(cr, "Sessions", val, KD_TEXT_PRIMARY, mx, &my, mw);

        snprintf(val, sizeof(val), "%ldK / %ldK",
                 g_mon.bytes_read / 1024, g_mon.bytes_written / 1024);
        draw_metric_row(cr, "I/O", val, KD_TEXT_PRIMARY, mx, &my, mw);

        my += 8;

        /* AI scheduler. */
        draw_section_title(cr, "AI Scheduler", mx, &my);

        snprintf(val, sizeof(val), "%d", g_mon.queue_depth);
        draw_metric_row(cr, "Queue depth", val,
                         g_mon.queue_depth > 0 ? KD_STATUS_WARNING : KD_STATUS_OK,
                         mx, &my, mw);

        snprintf(val, sizeof(val), "%ld", g_mon.total_submitted);
        draw_metric_row(cr, "Submitted", val, KD_TEXT_PRIMARY, mx, &my, mw);

        snprintf(val, sizeof(val), "%ld", g_mon.total_completed);
        draw_metric_row(cr, "Completed", val, KD_STATUS_OK, mx, &my, mw);
    } else {
        kd_draw_text(cr, "Module not loaded", mx, my,
                      KD_FONT_FAMILY, KD_FONT_SIZE_SMALL,
                      KD_TEXT_DIM, 0);
        my += 20;
    }

    my += 12;

    /* System section. */
    draw_section_title(cr, "System", mx, &my);

    if (g_mon.mem_total_kb > 0) {
        long used_mb = (g_mon.mem_total_kb - g_mon.mem_free_kb) / 1024;
        long total_mb = g_mon.mem_total_kb / 1024;
        double pct = (double)(g_mon.mem_total_kb - g_mon.mem_free_kb)
                     / (double)g_mon.mem_total_kb;

        char val[32];
        snprintf(val, sizeof(val), "%ldM / %ldM", used_mb, total_mb);
        draw_metric_row(cr, "Memory", val, KD_TEXT_PRIMARY, mx, &my, mw);

        /* Memory bar. */
        kd_draw_bar(cr, mx, my, mw - 32, 8, pct,
                     KD_BG_SURFACE, KD_ACCENT_GREEN);
        my += 18;
    }

    {
        char val[32];
        snprintf(val, sizeof(val), "%.2f", g_mon.load_avg);
        draw_metric_row(cr, "Load", val, KD_TEXT_PRIMARY, mx, &my, mw);
    }

    if (g_mon.kernel_version[0]) {
        draw_metric_row(cr, "Kernel", g_mon.kernel_version,
                         KD_TEXT_DIM, mx, &my, mw);
    }

    if (g_mon.uptime_sec > 0) {
        char val[32];
        long up = g_mon.uptime_sec;
        if (up > 3600)
            snprintf(val, sizeof(val), "%ldh %02ldm", up / 3600, (up % 3600) / 60);
        else
            snprintf(val, sizeof(val), "%ldm %02lds", up / 60, up % 60);
        draw_metric_row(cr, "Uptime", val, KD_TEXT_DIM, mx, &my, mw);
    }

    my += 12;

    /* Load graph. */
    draw_section_title(cr, "CPU Load (60s)", mx, &my);

    double graph_h = h - (my - y) - pad - 8;
    if (graph_h > 120) graph_h = 120;
    if (graph_h > 20) {
        draw_load_graph(cr, mx, my, mw - 32, graph_h);
    }
}

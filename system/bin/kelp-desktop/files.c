/*
 * kelp-desktop :: files.c
 * File browser: directory listing, breadcrumb nav, file preview.
 *
 * SPDX-License-Identifier: MIT
 */

#include "files.h"
#include "desktop.h"
#include "render.h"
#include "theme.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- Constants ---------------------------------------------------------- */

#define MAX_ENTRIES  512
#define ROW_HEIGHT   24
#define BREADCRUMB_H 32

/* ---- File entry --------------------------------------------------------- */

typedef struct {
    char  name[256];
    bool  is_dir;
    off_t size;
} file_entry_t;

/* ---- State -------------------------------------------------------------- */

static struct {
    char          cwd[1024];
    file_entry_t  entries[MAX_ENTRIES];
    int           entry_count;
    int           selected;
    int           scroll_offset;
} g_files;

/* ---- Directory reading -------------------------------------------------- */

static int entry_compare(const void *a, const void *b)
{
    const file_entry_t *ea = (const file_entry_t *)a;
    const file_entry_t *eb = (const file_entry_t *)b;
    /* Directories first, then alphabetical. */
    if (ea->is_dir != eb->is_dir)
        return eb->is_dir - ea->is_dir;
    return strcmp(ea->name, eb->name);
}

static void refresh_listing(void)
{
    g_files.entry_count = 0;
    g_files.selected = 0;
    g_files.scroll_offset = 0;

    /* Add parent directory entry. */
    if (strcmp(g_files.cwd, "/") != 0) {
        file_entry_t *e = &g_files.entries[g_files.entry_count++];
        snprintf(e->name, sizeof(e->name), "..");
        e->is_dir = true;
        e->size = 0;
    }

    DIR *dir = opendir(g_files.cwd);
    if (!dir) return;

    struct dirent *de;
    while ((de = readdir(dir)) != NULL && g_files.entry_count < MAX_ENTRIES) {
        if (de->d_name[0] == '.') continue; /* skip hidden */

        file_entry_t *e = &g_files.entries[g_files.entry_count];
        snprintf(e->name, sizeof(e->name), "%s", de->d_name);

        char full[2048];
        snprintf(full, sizeof(full), "%s/%s", g_files.cwd, de->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            e->is_dir = S_ISDIR(st.st_mode);
            e->size = st.st_size;
        } else {
            e->is_dir = (de->d_type == DT_DIR);
            e->size = 0;
        }
        g_files.entry_count++;
    }
    closedir(dir);

    /* Sort: dirs first, then alphabetical. */
    int start = (strcmp(g_files.cwd, "/") != 0) ? 1 : 0;
    if (g_files.entry_count > start) {
        qsort(&g_files.entries[start],
              (size_t)(g_files.entry_count - start),
              sizeof(file_entry_t), entry_compare);
    }
}

/* ---- Navigation --------------------------------------------------------- */

void kd_files_navigate(const char *path)
{
    snprintf(g_files.cwd, sizeof(g_files.cwd), "%s", path);
    refresh_listing();
}

static void navigate_to_entry(int idx)
{
    if (idx < 0 || idx >= g_files.entry_count) return;
    file_entry_t *e = &g_files.entries[idx];

    if (!e->is_dir) return; /* TODO: file preview */

    if (strcmp(e->name, "..") == 0) {
        /* Go up. */
        char *slash = strrchr(g_files.cwd, '/');
        if (slash && slash != g_files.cwd)
            *slash = '\0';
        else
            snprintf(g_files.cwd, sizeof(g_files.cwd), "/");
    } else {
        char tmp[2048];
        if (strcmp(g_files.cwd, "/") == 0)
            snprintf(tmp, sizeof(tmp), "/%s", e->name);
        else
            snprintf(tmp, sizeof(tmp), "%s/%s", g_files.cwd, e->name);
        snprintf(g_files.cwd, sizeof(g_files.cwd), "%s", tmp);
    }

    refresh_listing();
}

/* ---- Init/Shutdown ------------------------------------------------------ */

void kd_files_init(kd_desktop_t *d)
{
    (void)d;
    memset(&g_files, 0, sizeof(g_files));
    snprintf(g_files.cwd, sizeof(g_files.cwd), "/");
    refresh_listing();
}

void kd_files_shutdown(kd_desktop_t *d)
{
    (void)d;
}

/* ---- Drawing ------------------------------------------------------------ */

static void format_size(off_t size, char *buf, size_t bufsz)
{
    if (size >= 1024 * 1024)
        snprintf(buf, bufsz, "%.1fM", (double)size / (1024 * 1024));
    else if (size >= 1024)
        snprintf(buf, bufsz, "%.1fK", (double)size / 1024);
    else
        snprintf(buf, bufsz, "%lld", (long long)size);
}

void kd_files_draw(kd_desktop_t *d, cairo_t *cr,
                    double x, double y, double w, double h)
{
    (void)d;

    double pad = KD_PANEL_PADDING;
    double mx = x + pad;
    double my = y + pad;
    double mw = w - pad * 2;

    /* Breadcrumb bar. */
    kd_fill_rounded_rect(cr, mx, my, mw, BREADCRUMB_H, 4.0, KD_BG_SURFACE);
    kd_draw_text(cr, g_files.cwd, mx + 8, my + 8,
                  KD_FONT_MONO, KD_FONT_SIZE_SMALL,
                  KD_TEXT_PRIMARY, mw - 16);
    my += BREADCRUMB_H + 8;

    /* Column headers. */
    kd_draw_text(cr, "Name", mx + 28, my,
                  KD_FONT_FAMILY, KD_FONT_SIZE_SMALL,
                  KD_TEXT_DIM, 0);
    kd_draw_text(cr, "Size", mx + mw - 80, my,
                  KD_FONT_FAMILY, KD_FONT_SIZE_SMALL,
                  KD_TEXT_DIM, 0);
    my += 20;

    kd_draw_hline(cr, mx, my, mw, KD_BORDER);
    my += 4;

    /* File list. */
    double list_h = y + h - my - pad;
    int vis_rows = (int)(list_h / ROW_HEIGHT);

    cairo_save(cr);
    cairo_rectangle(cr, mx, my, mw, list_h);
    cairo_clip(cr);

    for (int i = g_files.scroll_offset;
         i < g_files.entry_count && (i - g_files.scroll_offset) < vis_rows;
         i++) {
        file_entry_t *e = &g_files.entries[i];
        double ry = my + (i - g_files.scroll_offset) * ROW_HEIGHT;

        /* Selection highlight. */
        if (i == g_files.selected) {
            kd_fill_rounded_rect(cr, mx, ry, mw, ROW_HEIGHT - 2,
                                  4.0, KD_BG_ELEVATED);
        }

        /* Icon indicator. */
        const char *icon = e->is_dir ? "D" : "F";
        kd_color_t icon_color = e->is_dir ? KD_ACCENT_GREEN : KD_TEXT_DIM;
        kd_draw_text(cr, icon, mx + 8, ry + 4,
                      KD_FONT_MONO, KD_FONT_SIZE_SMALL, icon_color, 0);

        /* Name. */
        kd_color_t name_color = e->is_dir ? KD_ACCENT_GREEN : KD_TEXT_PRIMARY;
        kd_draw_text(cr, e->name, mx + 28, ry + 4,
                      KD_FONT_FAMILY, KD_FONT_SIZE_SMALL,
                      name_color, mw - 120);

        /* Size. */
        if (!e->is_dir) {
            char szbuf[32];
            format_size(e->size, szbuf, sizeof(szbuf));
            kd_draw_text(cr, szbuf, mx + mw - 80, ry + 4,
                          KD_FONT_FAMILY, KD_FONT_SIZE_SMALL,
                          KD_TEXT_DIM, 0);
        }
    }

    cairo_restore(cr);

    /* Entry count. */
    char countbuf[32];
    snprintf(countbuf, sizeof(countbuf), "%d items", g_files.entry_count);
    kd_draw_text(cr, countbuf, mx, y + h - pad - 14,
                  KD_FONT_FAMILY, KD_FONT_SIZE_SMALL,
                  KD_TEXT_DIM, 0);
}

/* ---- Input handling ----------------------------------------------------- */

void kd_files_handle_key(kd_desktop_t *d, SDL_KeyboardEvent *key)
{
    if (key->type != SDL_KEYDOWN) return;

    switch (key->keysym.sym) {
    case SDLK_UP:
        if (g_files.selected > 0) g_files.selected--;
        d->needs_redraw = true;
        break;
    case SDLK_DOWN:
        if (g_files.selected < g_files.entry_count - 1) g_files.selected++;
        d->needs_redraw = true;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        navigate_to_entry(g_files.selected);
        d->needs_redraw = true;
        break;
    case SDLK_BACKSPACE:
        /* Navigate up. */
        navigate_to_entry(0); /* ".." is always first */
        d->needs_redraw = true;
        break;
    case SDLK_PAGEUP:
        g_files.selected -= 10;
        if (g_files.selected < 0) g_files.selected = 0;
        d->needs_redraw = true;
        break;
    case SDLK_PAGEDOWN:
        g_files.selected += 10;
        if (g_files.selected >= g_files.entry_count)
            g_files.selected = g_files.entry_count - 1;
        d->needs_redraw = true;
        break;
    default:
        break;
    }

    /* Scroll to keep selection visible. */
    int vis = 20; /* approximate visible rows */
    if (g_files.selected < g_files.scroll_offset)
        g_files.scroll_offset = g_files.selected;
    if (g_files.selected >= g_files.scroll_offset + vis)
        g_files.scroll_offset = g_files.selected - vis + 1;
}

void kd_files_handle_click(kd_desktop_t *d, double px, double py)
{
    double pad = KD_PANEL_PADDING;
    double list_start = pad + BREADCRUMB_H + 8 + 20 + 4;

    if (py < list_start) return;

    int row = (int)((py - list_start) / ROW_HEIGHT) + g_files.scroll_offset;
    if (row >= 0 && row < g_files.entry_count) {
        if (g_files.selected == row) {
            /* Double-click: navigate into directory. */
            navigate_to_entry(row);
        } else {
            g_files.selected = row;
        }
        d->needs_redraw = true;
    }
}

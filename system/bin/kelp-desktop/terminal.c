/*
 * kelp-desktop :: terminal.c
 * Terminal emulator: basic VT100, PTY via libkelp-process.
 *
 * Implements a basic terminal emulator with a character grid, VT100
 * escape sequence parsing, and PTY fork for running a shell.
 *
 * SPDX-License-Identifier: MIT
 */

#include "terminal.h"
#include "desktop.h"
#include "render.h"
#include "theme.h"

#include <kelp/log.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#ifdef __linux__
#include <pty.h>
#elif defined(__APPLE__)
#include <util.h>
#endif

/* ---- Constants ---------------------------------------------------------- */

#define TERM_MAX_COLS    256
#define TERM_MAX_ROWS    128
#define TERM_SCROLLBACK  1024
#define TERM_CELL_W      8
#define TERM_CELL_H      16
#define TERM_OUTPUT_SIZE  (64 * 1024)

/* ---- VT100 colors ------------------------------------------------------- */

static const kd_color_t vt_colors[16] = {
    /* Normal colors. */
    {0.102, 0.114, 0.149, 1.0},  /* 0: black */
    {0.914, 0.322, 0.322, 1.0},  /* 1: red */
    {0.384, 0.788, 0.384, 1.0},  /* 2: green */
    {0.914, 0.788, 0.322, 1.0},  /* 3: yellow */
    {0.384, 0.592, 0.914, 1.0},  /* 4: blue */
    {0.788, 0.384, 0.914, 1.0},  /* 5: magenta */
    {0.384, 0.788, 0.788, 1.0},  /* 6: cyan */
    {0.788, 0.812, 0.851, 1.0},  /* 7: white */
    /* Bright colors. */
    {0.369, 0.400, 0.467, 1.0},  /* 8: bright black */
    {1.000, 0.459, 0.459, 1.0},  /* 9: bright red */
    {0.459, 1.000, 0.459, 1.0},  /* 10: bright green */
    {1.000, 1.000, 0.459, 1.0},  /* 11: bright yellow */
    {0.459, 0.678, 1.000, 1.0},  /* 12: bright blue */
    {1.000, 0.459, 1.000, 1.0},  /* 13: bright magenta */
    {0.459, 1.000, 1.000, 1.0},  /* 14: bright cyan */
    {1.000, 1.000, 1.000, 1.0},  /* 15: bright white */
};

/* ---- Cell --------------------------------------------------------------- */

typedef struct {
    char     ch;
    uint8_t  fg;     /* color index 0-15 */
    uint8_t  bg;     /* color index 0-15 or 255 for transparent */
    bool     bold;
} term_cell_t;

/* ---- Terminal state ----------------------------------------------------- */

typedef struct {
    /* Character grid. */
    term_cell_t grid[TERM_MAX_ROWS][TERM_MAX_COLS];
    int  cols, rows;            /* current grid size */
    int  cursor_x, cursor_y;   /* cursor position */

    /* Attributes. */
    uint8_t cur_fg;
    uint8_t cur_bg;
    bool    cur_bold;

    /* PTY. */
    int     pty_master;
    pid_t   child_pid;
    bool    pty_active;

    /* Reader thread. */
    pthread_t reader_thread;
    bool      reader_running;

    /* Output capture. */
    char    output_buf[TERM_OUTPUT_SIZE];
    int     output_len;
    pthread_mutex_t output_lock;

    /* ESC sequence parser state. */
    enum { ST_NORMAL, ST_ESC, ST_CSI } parse_state;
    char    csi_buf[64];
    int     csi_len;

    /* Scroll. */
    int     scroll_top;
    int     scroll_bottom;
} term_state_t;

static term_state_t g_term;

/* ---- Grid helpers ------------------------------------------------------- */

static void term_clear_line(int row, int from, int to)
{
    for (int c = from; c < to && c < g_term.cols; c++) {
        g_term.grid[row][c].ch   = ' ';
        g_term.grid[row][c].fg   = 7;
        g_term.grid[row][c].bg   = 255;
        g_term.grid[row][c].bold = false;
    }
}

static void term_scroll_up(void)
{
    memmove(&g_term.grid[g_term.scroll_top],
            &g_term.grid[g_term.scroll_top + 1],
            sizeof(g_term.grid[0]) * (size_t)(g_term.scroll_bottom - g_term.scroll_top));
    term_clear_line(g_term.scroll_bottom, 0, g_term.cols);
}

static void term_newline(void)
{
    g_term.cursor_x = 0;
    if (g_term.cursor_y >= g_term.scroll_bottom) {
        term_scroll_up();
    } else {
        g_term.cursor_y++;
    }
}

/* ---- CSI parameter parsing ---------------------------------------------- */

static void parse_csi_params(const char *buf, int len, int *params, int *nparams)
{
    *nparams = 0;
    int val = 0;
    bool has_val = false;
    for (int i = 0; i < len; i++) {
        if (buf[i] >= '0' && buf[i] <= '9') {
            val = val * 10 + (buf[i] - '0');
            has_val = true;
        } else if (buf[i] == ';') {
            params[(*nparams)++] = has_val ? val : 0;
            val = 0;
            has_val = false;
            if (*nparams >= 16) break;
        }
    }
    if (has_val && *nparams < 16) {
        params[(*nparams)++] = val;
    }
}

/* ---- SGR (Select Graphic Rendition) ------------------------------------- */

static void handle_sgr(int *params, int nparams)
{
    if (nparams == 0) {
        g_term.cur_fg   = 7;
        g_term.cur_bg   = 255;
        g_term.cur_bold = false;
        return;
    }

    for (int i = 0; i < nparams; i++) {
        int p = params[i];
        if (p == 0) {
            g_term.cur_fg   = 7;
            g_term.cur_bg   = 255;
            g_term.cur_bold = false;
        } else if (p == 1) {
            g_term.cur_bold = true;
        } else if (p == 22) {
            g_term.cur_bold = false;
        } else if (p >= 30 && p <= 37) {
            g_term.cur_fg = (uint8_t)(p - 30);
        } else if (p == 39) {
            g_term.cur_fg = 7;
        } else if (p >= 40 && p <= 47) {
            g_term.cur_bg = (uint8_t)(p - 40);
        } else if (p == 49) {
            g_term.cur_bg = 255;
        } else if (p >= 90 && p <= 97) {
            g_term.cur_fg = (uint8_t)(p - 90 + 8);
        } else if (p >= 100 && p <= 107) {
            g_term.cur_bg = (uint8_t)(p - 100 + 8);
        }
    }
}

/* ---- CSI dispatch ------------------------------------------------------- */

static void handle_csi(char final)
{
    int params[16];
    int nparams;
    parse_csi_params(g_term.csi_buf, g_term.csi_len, params, &nparams);

    int n = (nparams > 0 && params[0] > 0) ? params[0] : 1;

    switch (final) {
    case 'A': /* CUU — cursor up */
        g_term.cursor_y -= n;
        if (g_term.cursor_y < 0) g_term.cursor_y = 0;
        break;
    case 'B': /* CUD — cursor down */
        g_term.cursor_y += n;
        if (g_term.cursor_y >= g_term.rows) g_term.cursor_y = g_term.rows - 1;
        break;
    case 'C': /* CUF — cursor forward */
        g_term.cursor_x += n;
        if (g_term.cursor_x >= g_term.cols) g_term.cursor_x = g_term.cols - 1;
        break;
    case 'D': /* CUB — cursor backward */
        g_term.cursor_x -= n;
        if (g_term.cursor_x < 0) g_term.cursor_x = 0;
        break;
    case 'H': /* CUP — cursor position */
    case 'f':
        {
            int row = (nparams > 0 && params[0] > 0) ? params[0] - 1 : 0;
            int col = (nparams > 1 && params[1] > 0) ? params[1] - 1 : 0;
            if (row >= g_term.rows) row = g_term.rows - 1;
            if (col >= g_term.cols) col = g_term.cols - 1;
            g_term.cursor_y = row;
            g_term.cursor_x = col;
        }
        break;
    case 'J': /* ED — erase in display */
        {
            int mode = (nparams > 0) ? params[0] : 0;
            if (mode == 0) {
                /* Clear from cursor to end. */
                term_clear_line(g_term.cursor_y, g_term.cursor_x, g_term.cols);
                for (int r = g_term.cursor_y + 1; r < g_term.rows; r++)
                    term_clear_line(r, 0, g_term.cols);
            } else if (mode == 1) {
                /* Clear from start to cursor. */
                for (int r = 0; r < g_term.cursor_y; r++)
                    term_clear_line(r, 0, g_term.cols);
                term_clear_line(g_term.cursor_y, 0, g_term.cursor_x + 1);
            } else if (mode == 2) {
                /* Clear entire screen. */
                for (int r = 0; r < g_term.rows; r++)
                    term_clear_line(r, 0, g_term.cols);
            }
        }
        break;
    case 'K': /* EL — erase in line */
        {
            int mode = (nparams > 0) ? params[0] : 0;
            if (mode == 0)
                term_clear_line(g_term.cursor_y, g_term.cursor_x, g_term.cols);
            else if (mode == 1)
                term_clear_line(g_term.cursor_y, 0, g_term.cursor_x + 1);
            else if (mode == 2)
                term_clear_line(g_term.cursor_y, 0, g_term.cols);
        }
        break;
    case 'm': /* SGR — select graphic rendition */
        handle_sgr(params, nparams);
        break;
    case 'r': /* DECSTBM — set scrolling region */
        {
            int top = (nparams > 0 && params[0] > 0) ? params[0] - 1 : 0;
            int bot = (nparams > 1 && params[1] > 0) ? params[1] - 1 : g_term.rows - 1;
            if (top < bot && bot < g_term.rows) {
                g_term.scroll_top = top;
                g_term.scroll_bottom = bot;
            }
        }
        break;
    case 'L': /* IL — insert lines */
        /* Simplified: just clear. */
        for (int i = 0; i < n && g_term.cursor_y + i < g_term.rows; i++)
            term_clear_line(g_term.cursor_y + i, 0, g_term.cols);
        break;
    default:
        break;
    }
}

/* ---- Character processing ----------------------------------------------- */

static void term_putchar(char ch)
{
    switch (g_term.parse_state) {
    case ST_NORMAL:
        if (ch == '\033') {
            g_term.parse_state = ST_ESC;
        } else if (ch == '\n') {
            term_newline();
        } else if (ch == '\r') {
            g_term.cursor_x = 0;
        } else if (ch == '\b') {
            if (g_term.cursor_x > 0) g_term.cursor_x--;
        } else if (ch == '\t') {
            g_term.cursor_x = (g_term.cursor_x + 8) & ~7;
            if (g_term.cursor_x >= g_term.cols)
                g_term.cursor_x = g_term.cols - 1;
        } else if (ch >= 32) {
            if (g_term.cursor_x >= g_term.cols)
                term_newline();
            g_term.grid[g_term.cursor_y][g_term.cursor_x] = (term_cell_t){
                .ch   = ch,
                .fg   = g_term.cur_fg,
                .bg   = g_term.cur_bg,
                .bold = g_term.cur_bold,
            };
            g_term.cursor_x++;
        }
        break;

    case ST_ESC:
        if (ch == '[') {
            g_term.parse_state = ST_CSI;
            g_term.csi_len = 0;
        } else if (ch == 'c') {
            /* Reset. */
            g_term.cur_fg   = 7;
            g_term.cur_bg   = 255;
            g_term.cur_bold = false;
            g_term.parse_state = ST_NORMAL;
        } else {
            g_term.parse_state = ST_NORMAL;
        }
        break;

    case ST_CSI:
        if (ch >= 0x40 && ch <= 0x7E) {
            /* Final byte. */
            handle_csi(ch);
            g_term.parse_state = ST_NORMAL;
        } else {
            if (g_term.csi_len < (int)sizeof(g_term.csi_buf) - 1)
                g_term.csi_buf[g_term.csi_len++] = ch;
        }
        break;
    }
}

static void term_write(const char *data, int len)
{
    for (int i = 0; i < len; i++)
        term_putchar(data[i]);
}

/* ---- PTY reader thread -------------------------------------------------- */

static void *pty_reader_fn(void *arg)
{
    (void)arg;
    char buf[4096];

    while (g_term.reader_running) {
        struct pollfd pfd = { .fd = g_term.pty_master, .events = POLLIN };
        int ret = poll(&pfd, 1, 100);
        if (ret <= 0) continue;

        ssize_t n = read(g_term.pty_master, buf, sizeof(buf) - 1);
        if (n <= 0) {
            if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
                g_term.pty_active = false;
                break;
            }
            continue;
        }

        pthread_mutex_lock(&g_term.output_lock);
        term_write(buf, (int)n);

        /* Also append to output capture buffer. */
        if (g_term.output_len + (int)n < TERM_OUTPUT_SIZE - 1) {
            memcpy(g_term.output_buf + g_term.output_len, buf, (size_t)n);
            g_term.output_len += (int)n;
            g_term.output_buf[g_term.output_len] = '\0';
        }
        pthread_mutex_unlock(&g_term.output_lock);
    }

    return NULL;
}

/* ---- Init/Shutdown ------------------------------------------------------ */

void kd_terminal_init(kd_desktop_t *d)
{
    (void)d;
    memset(&g_term, 0, sizeof(g_term));
    g_term.cols = 80;
    g_term.rows = 24;
    g_term.cursor_x = 0;
    g_term.cursor_y = 0;
    g_term.cur_fg = 7;
    g_term.cur_bg = 255;
    g_term.scroll_top = 0;
    g_term.scroll_bottom = g_term.rows - 1;
    g_term.pty_master = -1;
    pthread_mutex_init(&g_term.output_lock, NULL);

    /* Clear grid. */
    for (int r = 0; r < g_term.rows; r++)
        term_clear_line(r, 0, g_term.cols);

    /* Fork PTY. */
    struct winsize ws = {
        .ws_row = (unsigned short)g_term.rows,
        .ws_col = (unsigned short)g_term.cols,
    };

    g_term.child_pid = forkpty(&g_term.pty_master, NULL, NULL, &ws);
    if (g_term.child_pid < 0) {
        KELP_ERROR("terminal: forkpty failed: %s", strerror(errno));
        return;
    }

    if (g_term.child_pid == 0) {
        /* Child: exec shell. */
        setenv("TERM", "xterm-256color", 1);
        setenv("PS1", "\\[\\033[32m\\]kelp\\[\\033[0m\\]$ ", 1);
        const char *shell = getenv("SHELL");
        if (!shell) shell = "/bin/sh";
        execl(shell, shell, (char *)NULL);
        _exit(127);
    }

    /* Parent. */
    int flags = fcntl(g_term.pty_master, F_GETFL, 0);
    fcntl(g_term.pty_master, F_SETFL, flags | O_NONBLOCK);

    g_term.pty_active = true;
    g_term.reader_running = true;
    pthread_create(&g_term.reader_thread, NULL, pty_reader_fn, NULL);
}

void kd_terminal_shutdown(kd_desktop_t *d)
{
    (void)d;
    g_term.reader_running = false;
    if (g_term.pty_active)
        pthread_join(g_term.reader_thread, NULL);

    if (g_term.pty_master >= 0)
        close(g_term.pty_master);

    if (g_term.child_pid > 0) {
        kill(g_term.child_pid, SIGTERM);
        waitpid(g_term.child_pid, NULL, WNOHANG);
    }

    pthread_mutex_destroy(&g_term.output_lock);
}

/* ---- Inject text (AI control) ------------------------------------------- */

void kd_terminal_inject_text(const char *text)
{
    if (!g_term.pty_active || g_term.pty_master < 0) return;
    write(g_term.pty_master, text, strlen(text));
}

const char *kd_terminal_get_output(void)
{
    return g_term.output_buf;
}

/* ---- Update ------------------------------------------------------------- */

void kd_terminal_update(kd_desktop_t *d, uint32_t now_ms)
{
    (void)now_ms;

    /* Check if child died. */
    if (g_term.child_pid > 0) {
        int status;
        pid_t ret = waitpid(g_term.child_pid, &status, WNOHANG);
        if (ret > 0) {
            g_term.pty_active = false;
            d->needs_redraw = true;
        }
    }
    d->needs_redraw = true; /* terminal always needs redraw for cursor blink etc. */
}

/* ---- Drawing ------------------------------------------------------------ */

void kd_terminal_draw(kd_desktop_t *d, cairo_t *cr,
                       double x, double y, double w, double h)
{
    (void)d;

    double pad = 8;
    double gx = x + pad;
    double gy = y + pad;

    /* Recalculate grid size based on panel dimensions. */
    int vis_cols = (int)((w - pad * 2) / TERM_CELL_W);
    int vis_rows = (int)((h - pad * 2) / TERM_CELL_H);
    if (vis_cols > TERM_MAX_COLS) vis_cols = TERM_MAX_COLS;
    if (vis_rows > TERM_MAX_ROWS) vis_rows = TERM_MAX_ROWS;

    pthread_mutex_lock(&g_term.output_lock);

    /* Draw grid cells. */
    for (int r = 0; r < vis_rows && r < g_term.rows; r++) {
        for (int c = 0; c < vis_cols && c < g_term.cols; c++) {
            term_cell_t *cell = &g_term.grid[r][c];
            double cx = gx + c * TERM_CELL_W;
            double cy = gy + r * TERM_CELL_H;

            /* Background. */
            if (cell->bg != 255 && cell->bg < 16) {
                kd_fill_rect(cr, cx, cy, TERM_CELL_W, TERM_CELL_H,
                              vt_colors[cell->bg]);
            }

            /* Character. */
            if (cell->ch > 32) {
                char tmp[2] = { cell->ch, '\0' };
                int fg_idx = cell->fg;
                if (cell->bold && fg_idx < 8)
                    fg_idx += 8;
                kd_color_t fg_color = (fg_idx < 16) ? vt_colors[fg_idx]
                                                      : KD_TEXT_PRIMARY;
                kd_draw_mono_text(cr, tmp, cx, cy,
                                   KD_FONT_SIZE_MONO, fg_color, 0);
            }
        }
    }

    /* Cursor. */
    if (g_term.pty_active) {
        double ccx = gx + g_term.cursor_x * TERM_CELL_W;
        double ccy = gy + g_term.cursor_y * TERM_CELL_H;
        kd_color_t cursor_color = KD_ACCENT_GREEN;
        cursor_color.a = 0.6;
        kd_fill_rect(cr, ccx, ccy, TERM_CELL_W, TERM_CELL_H, cursor_color);
    }

    pthread_mutex_unlock(&g_term.output_lock);

    /* "Shell exited" indicator. */
    if (!g_term.pty_active) {
        kd_draw_text(cr, "[shell exited]", gx, gy + vis_rows * TERM_CELL_H + 4,
                      KD_FONT_MONO, KD_FONT_SIZE_SMALL, KD_TEXT_DIM, 0);
    }
}

/* ---- Input handling ----------------------------------------------------- */

void kd_terminal_handle_key(kd_desktop_t *d, SDL_KeyboardEvent *key)
{
    if (!g_term.pty_active || g_term.pty_master < 0) return;
    if (key->type != SDL_KEYDOWN) return;

    SDL_Keycode sym = key->keysym.sym;
    char buf[8];
    int len = 0;

    switch (sym) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        buf[0] = '\r'; len = 1;
        break;
    case SDLK_BACKSPACE:
        buf[0] = 0x7f; len = 1;
        break;
    case SDLK_TAB:
        buf[0] = '\t'; len = 1;
        break;
    case SDLK_ESCAPE:
        buf[0] = '\033'; len = 1;
        break;
    case SDLK_UP:
        memcpy(buf, "\033[A", 3); len = 3;
        break;
    case SDLK_DOWN:
        memcpy(buf, "\033[B", 3); len = 3;
        break;
    case SDLK_RIGHT:
        memcpy(buf, "\033[C", 3); len = 3;
        break;
    case SDLK_LEFT:
        memcpy(buf, "\033[D", 3); len = 3;
        break;
    case SDLK_HOME:
        memcpy(buf, "\033[H", 3); len = 3;
        break;
    case SDLK_END:
        memcpy(buf, "\033[F", 3); len = 3;
        break;
    case SDLK_DELETE:
        memcpy(buf, "\033[3~", 4); len = 4;
        break;
    case SDLK_c:
        if (key->keysym.mod & KMOD_CTRL) {
            buf[0] = 3; len = 1; /* ETX */
        }
        break;
    case SDLK_d:
        if (key->keysym.mod & KMOD_CTRL) {
            buf[0] = 4; len = 1; /* EOT */
        }
        break;
    case SDLK_z:
        if (key->keysym.mod & KMOD_CTRL) {
            buf[0] = 26; len = 1; /* SUB */
        }
        break;
    case SDLK_l:
        if (key->keysym.mod & KMOD_CTRL) {
            buf[0] = 12; len = 1; /* FF (clear) */
        }
        break;
    default:
        break;
    }

    if (len > 0) {
        write(g_term.pty_master, buf, (size_t)len);
        d->needs_redraw = true;
    }
}

void kd_terminal_handle_text(kd_desktop_t *d, const char *text)
{
    if (!g_term.pty_active || g_term.pty_master < 0) return;
    write(g_term.pty_master, text, strlen(text));
    d->needs_redraw = true;
}

void kd_terminal_handle_click(kd_desktop_t *d, double px, double py)
{
    (void)d;
    (void)px;
    (void)py;
}

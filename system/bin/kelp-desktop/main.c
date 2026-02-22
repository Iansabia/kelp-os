/*
 * kelp-desktop — Kelp OS Graphical Desktop Shell
 *
 * SDL2/DRM init, main event loop (16ms tick), Cairo/SDL bridge.
 * Renders a full graphical desktop with AI chat, terminal, system
 * monitor, and file browser panels.
 *
 * SPDX-License-Identifier: MIT
 */

#include "desktop.h"
#include "render.h"
#include "theme.h"
#include "topbar.h"
#include "dock.h"
#include "chat.h"
#include "terminal.h"
#include "monitor.h"
#include "files.h"
#include "cursor.h"
#include "ai_control.h"
#include "animation.h"

#include <kelp/kelp.h>
#include <kelp/config.h>
#include <kelp/paths.h>
#include <kelp/log.h>

#include <SDL2/SDL.h>
#include <cairo/cairo.h>

#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- Constants ---------------------------------------------------------- */

#define KELP_DESKTOP_VERSION "1.0.0"
#define TARGET_FPS           60
#define FRAME_TIME_MS        (1000 / TARGET_FPS)
#define DEFAULT_WIDTH        1280
#define DEFAULT_HEIGHT       800

/* ---- Global state ------------------------------------------------------- */

static kd_desktop_t   g_desktop;
static kelp_config_t  g_cfg;
static volatile sig_atomic_t g_quit = 0;

/* ---- Signal handler ----------------------------------------------------- */

static void sig_handler(int sig)
{
    (void)sig;
    g_quit = 1;
}

/* ---- SDL + Cairo bridge ------------------------------------------------- */

static int init_sdl(kd_desktop_t *d)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "kelp-desktop: SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Try to get display bounds for fullscreen sizing. */
    int width  = DEFAULT_WIDTH;
    int height = DEFAULT_HEIGHT;

    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
        width  = dm.w;
        height = dm.h;
    }

    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;

    /* If running on KMS/DRM, use fullscreen. */
    const char *driver = SDL_GetCurrentVideoDriver();
    if (driver && strcmp(driver, "KMSDRM") == 0) {
        flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    d->window = SDL_CreateWindow(
        "Kelp OS",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, flags);
    if (!d->window) {
        fprintf(stderr, "kelp-desktop: SDL_CreateWindow failed: %s\n",
                SDL_GetError());
        SDL_Quit();
        return -1;
    }

    d->renderer = SDL_CreateRenderer(d->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!d->renderer) {
        /* Fallback to software renderer. */
        d->renderer = SDL_CreateRenderer(d->window, -1,
            SDL_RENDERER_SOFTWARE);
    }
    if (!d->renderer) {
        fprintf(stderr, "kelp-desktop: SDL_CreateRenderer failed: %s\n",
                SDL_GetError());
        SDL_DestroyWindow(d->window);
        SDL_Quit();
        return -1;
    }

    SDL_GetWindowSize(d->window, &d->screen_w, &d->screen_h);

    d->texture = SDL_CreateTexture(d->renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        d->screen_w, d->screen_h);
    if (!d->texture) {
        fprintf(stderr, "kelp-desktop: SDL_CreateTexture failed: %s\n",
                SDL_GetError());
        SDL_DestroyRenderer(d->renderer);
        SDL_DestroyWindow(d->window);
        SDL_Quit();
        return -1;
    }

    SDL_ShowCursor(SDL_DISABLE); /* We render our own cursor. */
    SDL_StartTextInput();

    return 0;
}

static void recreate_texture(kd_desktop_t *d)
{
    if (d->texture) SDL_DestroyTexture(d->texture);
    d->texture = SDL_CreateTexture(d->renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        d->screen_w, d->screen_h);
}

static void render_frame(kd_desktop_t *d)
{
    void *pixels;
    int pitch;

    if (SDL_LockTexture(d->texture, NULL, &pixels, &pitch) < 0)
        return;

    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        (unsigned char *)pixels, CAIRO_FORMAT_ARGB32,
        d->screen_w, d->screen_h, pitch);
    cairo_t *cr = cairo_create(surface);

    /* Clear background. */
    kd_color_t bg = KD_BG_PRIMARY;
    cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, bg.a);
    cairo_paint(cr);

    /* Draw desktop components. */
    kd_topbar_draw(d, cr);
    kd_dock_draw(d, cr);
    kd_desktop_draw_panels(d, cr);
    kd_cursor_draw(d, cr);

    /* Boot fade-in overlay. */
    if (!d->boot_done) {
        double alpha = 1.0 - d->boot_anim.current;
        if (alpha > 0.0) {
            cairo_set_source_rgba(cr, 0, 0, 0, alpha);
            cairo_paint(cr);

            /* Kelp logo during boot. */
            if (alpha > 0.3) {
                kd_color_t logo_color = KD_ACCENT_GREEN;
                logo_color.a = alpha;
                kd_draw_text_bold(cr, "KELP OS",
                    d->screen_w / 2.0 - 60, d->screen_h / 2.0 - 20,
                    KD_FONT_FAMILY, 32.0, logo_color, 0);
            }
        }
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    SDL_UnlockTexture(d->texture);

    SDL_RenderCopy(d->renderer, d->texture, NULL, NULL);
    SDL_RenderPresent(d->renderer);
}

/* ---- Usage -------------------------------------------------------------- */

static void usage(void)
{
    printf(
        "Usage: kelp-desktop [options]\n"
        "\n"
        "Options:\n"
        "  -c, --config <path>   Configuration file path\n"
        "  -h, --help            Show this help\n"
        "\n"
        "Kelp OS graphical desktop shell.\n"
    );
}

/* ---- Main --------------------------------------------------------------- */

int main(int argc, char **argv)
{
    const char *config_path = NULL;

    static struct option long_options[] = {
        {"config", required_argument, NULL, 'c'},
        {"help",   no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'c': config_path = optarg; break;
        case 'h':
            usage();
            return 0;
        default:
            return 1;
        }
    }

    /* Load configuration. */
    memset(&g_cfg, 0, sizeof(g_cfg));
    if (config_path) {
        if (kelp_config_load(config_path, &g_cfg) != 0) {
            fprintf(stderr, "kelp-desktop: failed to load config: %s\n",
                    config_path);
            return 1;
        }
    } else {
        kelp_config_load_default(&g_cfg);
    }
    kelp_config_merge_env(&g_cfg);

    kelp_log_init("kelp-desktop", KELP_LOG_WARN);
    if (g_cfg.logging.file) {
        FILE *logfp = fopen(g_cfg.logging.file, "a");
        if (logfp) kelp_log_set_file(logfp);
    }

    /* Set up signal handling. */
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    /* Initialize desktop. */
    memset(&g_desktop, 0, sizeof(g_desktop));

    if (init_sdl(&g_desktop) != 0) {
        fprintf(stderr, "kelp-desktop: failed to initialize display\n");
        return 1;
    }

    kd_desktop_init(&g_desktop);
    kd_chat_init(&g_desktop, &g_cfg);
    kd_terminal_init(&g_desktop);
    kd_monitor_init(&g_desktop);
    kd_files_init(&g_desktop);
    kd_cursor_init(&g_desktop);
    kd_ai_control_init(&g_desktop);

    /* Connect to gateway. */
    kd_chat_connect_gateway(&g_desktop, &g_cfg);

    g_desktop.running = true;

    /* Layout panels. */
    kd_desktop_layout(&g_desktop);

    /* Initial render. */
    render_frame(&g_desktop);

    /* Main event loop — 60fps. */
    while (g_desktop.running && !g_quit) {
        uint32_t frame_start = SDL_GetTicks();

        /* Process events. */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            kd_desktop_handle_event(&g_desktop, &event);
        }

        /* Handle window resize — recreate texture. */
        int cur_w, cur_h;
        SDL_GetWindowSize(g_desktop.window, &cur_w, &cur_h);
        if (cur_w != g_desktop.screen_w || cur_h != g_desktop.screen_h) {
            g_desktop.screen_w = cur_w;
            g_desktop.screen_h = cur_h;
            recreate_texture(&g_desktop);
            kd_desktop_layout(&g_desktop);
            g_desktop.needs_redraw = true;
        }

        /* Update state. */
        uint32_t now = SDL_GetTicks();
        kd_desktop_update(&g_desktop, now);

        /* Render. */
        if (g_desktop.needs_redraw || !g_desktop.boot_done) {
            render_frame(&g_desktop);
            g_desktop.needs_redraw = false;
            g_desktop.frame_count++;
        }

        /* Frame timing. */
        uint32_t frame_time = SDL_GetTicks() - frame_start;
        if (frame_time < FRAME_TIME_MS)
            SDL_Delay(FRAME_TIME_MS - frame_time);
    }

    /* Cleanup. */
    kd_ai_control_shutdown(&g_desktop);
    kd_cursor_shutdown(&g_desktop);
    kd_files_shutdown(&g_desktop);
    kd_monitor_shutdown(&g_desktop);
    kd_terminal_shutdown(&g_desktop);
    kd_chat_shutdown(&g_desktop);

    if (g_desktop.texture)  SDL_DestroyTexture(g_desktop.texture);
    if (g_desktop.renderer) SDL_DestroyRenderer(g_desktop.renderer);
    if (g_desktop.window)   SDL_DestroyWindow(g_desktop.window);
    SDL_Quit();

    kelp_config_free(&g_cfg);
    return 0;
}

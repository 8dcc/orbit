
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL2/SDL.h>

#define GRID_W 640
#define GRID_H 480
#define FPS    60

#define LENGTH(ARR) (sizeof(ARR) / sizeof((ARR)[0]))

/*----------------------------------------------------------------------------*/
/* Enums and structs */

typedef enum EBodyType {
    BODY_STATIC  = 0, /* It can't move */
    BODY_DYNAMIC = 1, /* It can move */
} EBodyType;

typedef struct Body {
    /* Next body in the linked list */
    struct Body* next;

    /* The body type determines whether it can move or not. The color will
     * change depending on the type when rendering. */
    EBodyType type;

    /* X and Y positions */
    float x, y;

    /* X and Y velocity */
    float vel_x, vel_y;

    /* The mass will determine the attraction force of the body, and it's size
     * when rendering. */
    float mass;
} Body;

/*----------------------------------------------------------------------------*/
/* Globals */

static Body* bodies = NULL;

static uint32_t color_palette[] = {
    [BODY_STATIC]  = 0x555555,
    [BODY_DYNAMIC] = 0xCCCCCC,
};

/*----------------------------------------------------------------------------*/
/* Misc utils */

static void die(const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);

    vfprintf(stderr, fmt, va);
    putc('\n', stderr);

    SDL_Quit();
    exit(1);
}

/*----------------------------------------------------------------------------*/
/* SDL utils */

static inline void set_render_color(SDL_Renderer* rend, uint32_t col) {
    const uint8_t r = (col >> 16) & 0xFF;
    const uint8_t g = (col >> 8) & 0xFF;
    const uint8_t b = (col >> 0) & 0xFF;
    const uint8_t a = 255;
    SDL_SetRenderDrawColor(rend, r, g, b, a);
}

/* Credits for both circle functions: @Gumichan01
 * https://gist.github.com/Gumichan01/332c26f6197a432db91cc4327fcabb1c */
static int draw_circle(SDL_Renderer* rend, int x, int y, int r, uint32_t col) {
    int dx     = 0;
    int dy     = r;
    int d      = r - 1;
    int status = 0;

    set_render_color(rend, col);

    while (dy >= dx) {
        status += SDL_RenderDrawPoint(rend, x + dx, y + dy);
        status += SDL_RenderDrawPoint(rend, x + dy, y + dx);
        status += SDL_RenderDrawPoint(rend, x - dx, y + dy);
        status += SDL_RenderDrawPoint(rend, x - dy, y + dx);
        status += SDL_RenderDrawPoint(rend, x + dx, y - dy);
        status += SDL_RenderDrawPoint(rend, x + dy, y - dx);
        status += SDL_RenderDrawPoint(rend, x - dx, y - dy);
        status += SDL_RenderDrawPoint(rend, x - dy, y - dx);

        if (status < 0) {
            status = -1;
            break;
        }

        if (d >= 2 * dx) {
            d -= 2 * dx + 1;
            dx += 1;
        } else if (d < 2 * (r - dy)) {
            d += 2 * dy - 1;
            dy -= 1;
        } else {
            d += 2 * (dy - dx - 1);
            dy -= 1;
            dx += 1;
        }
    }

    return status;
}

static int draw_circle_filled(SDL_Renderer* rend, int x, int y, int r,
                              uint32_t col) {
    int dx     = 0;
    int dy     = r;
    int d      = r - 1;
    int status = 0;

    set_render_color(rend, col);

    while (dy >= dx) {
        status += SDL_RenderDrawLine(rend, x - dy, y + dx, x + dy, y + dx);
        status += SDL_RenderDrawLine(rend, x - dx, y + dy, x + dx, y + dy);
        status += SDL_RenderDrawLine(rend, x - dx, y - dy, x + dx, y - dy);
        status += SDL_RenderDrawLine(rend, x - dy, y - dx, x + dy, y - dx);

        if (status < 0) {
            status = -1;
            break;
        }

        if (d >= 2 * dx) {
            d -= 2 * dx + 1;
            dx += 1;
        } else if (d < 2 * (r - dy)) {
            d += 2 * dy - 1;
            dy -= 1;
        } else {
            d += 2 * (dy - dx - 1);
            dy -= 1;
            dx += 1;
        }
    }

    return status;
}

/*----------------------------------------------------------------------------*/
/* Orbit functions */

static Body* get_last_body(void) {
    Body* body = bodies;

    /* Caller should make sure this is never the case */
    if (!body)
        return NULL;

    while (body->next != NULL)
        body = body->next;

    return body;
}

static void add_body(float x, float y, EBodyType type) {
    /* Allocate new Body */
    Body* new_body  = malloc(sizeof(Body));
    new_body->type  = type;
    new_body->mass  = 7.f;
    new_body->x     = x;
    new_body->y     = y;
    new_body->vel_x = 0.f;
    new_body->vel_y = 0.f;
    new_body->next  = NULL;

    /* Add to the END of the linked list of Body structs. This is important so
     * the latter bodies are rendered on top of the previous ones. */
    if (bodies == NULL) {
        /* This is the first body, overwrite `bodies' */
        bodies = new_body;
    } else {
        /* Otherwise, append to the linked list */
        Body* last_body = get_last_body();
        last_body->next = new_body;
    }
}

/*
 * The gravitational force of each body is calculated with this formula:
 *    F = G * (m1 * m2) / r^2
 * Where 'G' is the gravitational constant, 'm1' and 'm2' are the mass of each
 * body, and 'r' is the distance between the objects. In this case, we can skip
 * this 'G' constant since we are using pixels.
 *
 * The effect of a force is to accelerate the body. The relationship is the
 * following:
 *    F = m * a
 * Where 'F' is the force, 'm' is the mass and 'a' is the acceleration of the
 * body. Therefore, to get the acceleration from the force:
 *    a = F / m
 *
 * TODO: Continue explanation and add function for handling attractions.
 */

static void move_bodies(void) {
    for (Body* body = bodies; body != NULL; body = body->next) {
        body->x += body->vel_x;
        body->y += body->vel_y;
    }
}

static void render_grid(SDL_Renderer* rend) {
    for (Body* body = bodies; body != NULL; body = body->next) {
        assert(body->type < LENGTH(color_palette));

        /* Round float positions to get the grid coordinates */
        const int x = (int)roundf(body->x);
        const int y = (int)roundf(body->y);

        /* Round mass to get the circle radius */
        const int radius = (int)roundf(body->mass);

        const uint32_t color = color_palette[body->type];

        if (body->type == BODY_STATIC)
            draw_circle(rend, x, y, radius, color);
        else
            draw_circle_filled(rend, x, y, radius, color);
    }
}

/*----------------------------------------------------------------------------*/

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
        die("Unable to start SDL.");

    /* Create SDL window */
    SDL_Window* sdl_window =
      SDL_CreateWindow("Orbit", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                       GRID_W, GRID_H, 0);
    if (!sdl_window)
        die("Error creating SDL window.");

    /* Create SDL renderer */
    SDL_Renderer* sdl_renderer =
      SDL_CreateRenderer(sdl_window, -1,
                         SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_renderer) {
        SDL_DestroyWindow(sdl_window);
        die("Error creating SDL renderer.");
    }

    /* Main loop */
    bool running = true;
    while (running) {
        /* Parse SDL events */
        SDL_Event sdl_event;
        while (SDL_PollEvent(&sdl_event)) {
            switch (sdl_event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    switch (sdl_event.key.keysym.scancode) {
                        case SDL_SCANCODE_ESCAPE:
                        case SDL_SCANCODE_Q:
                            running = false;
                            break;
                        default:
                            break;
                    }      /* End scancode switch */
                    break; /* End SDL_KEYDOWN case */
                case SDL_MOUSEBUTTONUP: {
                    const int mouse_x = sdl_event.motion.x;
                    const int mouse_y = sdl_event.motion.y;

                    switch (sdl_event.button.button) {
                        case SDL_BUTTON_LEFT:
                            add_body(mouse_x, mouse_y, BODY_DYNAMIC);
                            break;
                        case SDL_BUTTON_RIGHT:
                            add_body(mouse_x, mouse_y, BODY_STATIC);
                            break;
                        default:
                            break;
                    }    /* End mouse button switch */
                } break; /* End SDL_MOUSEBUTTON case */
                default:
                    break;
            } /* End event.type switch */
        }     /* End PollEvent while */

        /* Clear window */
        set_render_color(sdl_renderer, 0x000000);
        SDL_RenderClear(sdl_renderer);

        /* DELME: Testing */
        draw_circle(sdl_renderer, 50, 100, 40, 0xFF0000);
        draw_circle_filled(sdl_renderer, 140, 100, 40, 0xFF0000);

        /* Apply the velocity of each body */
        move_bodies();

        /* Render the valid bodies */
        render_grid(sdl_renderer);

        /* Send to renderer and delay depending on FPS */
        SDL_RenderPresent(sdl_renderer);
        SDL_Delay(1000 / FPS);
    }

    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();

    return 0;
}

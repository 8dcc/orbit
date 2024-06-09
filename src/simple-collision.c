
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL2/SDL.h>

#define GRID_W 640
#define GRID_H 480
#define FPS    30

#define CURRENT_MASS_STEP 2.f

#define START_VEL_X 0.f
#define START_VEL_Y -1.f

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

/* Linked list of Body structures */
static Body* bodies = NULL;

/* Current mass for new bodies. Controlled with MWheel or 1/2. */
static float current_mass = 7.f;

/* Color palette for different types of bodies */
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
    /* Allocate new Body. The current mass is changed by the user, see comment
     * in global variable. */
    Body* new_body  = malloc(sizeof(Body));
    new_body->type  = type;
    new_body->mass  = current_mass;
    new_body->x     = x;
    new_body->y     = y;
    new_body->vel_x = START_VEL_X;
    new_body->vel_y = START_VEL_Y;
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

/* Calculate new velocity of to body 'a', relative to 'b' */
static void apply_bounce(Body* a, Body* b) {
    /* For now, the widths are the masses */
    const float a_width = a->mass;
    const float b_width = b->mass;

    /*
     * NOTE: For more information on the math behind this function, see the file
     * `../collision.tex' and `../collision.pdf'.
     */
    float dx       = b->x - a->x;
    float dy       = b->y - a->y;
    float distance = sqrtf(dx * dx + dy * dy);

    /* Are the bodies colliding */
    if (a_width + b_width < distance)
        return;

    /* Calculate the reflection angle and bounce back with the new
     * velocity. */
    float nx = dx / distance;
    float ny = dy / distance;

    float dot_product = a->vel_x * nx + a->vel_y * ny;
    float nvx         = dot_product * nx;
    float nvy         = dot_product * ny;

    float perpendicular_x = a->vel_x - nvx;
    float perpendicular_y = a->vel_y - nvy;

    a->vel_x = perpendicular_x - nvx;
    a->vel_y = perpendicular_y - nvy;
}

/* Calculate velocities of all bodies in case they are colliding */
static void apply_bounces(void) {
    /* NOTE: This is a very bad iterative method, since some operations are
     * repeated. However, it's more clear this way, so I decided to leave it
     * like this. */
    for (Body* a = bodies; a != NULL; a = a->next) {
        /* Static bodies don't move */
        if (a->type == BODY_STATIC)
            continue;

        for (Body* b = bodies; b != NULL; b = b->next) {
            if (a == b)
                continue;

            apply_bounce(a, b);
        }
    }
}

static void move_bodies(void) {
    for (Body* body = bodies; body != NULL; body = body->next) {
        /* Static bodies don't move */
        if (body->type == BODY_STATIC)
            continue;

        body->x += body->vel_x;
        body->y += body->vel_y;
    }
}

static void render_bodies(SDL_Renderer* rend) {
    for (Body* a = bodies; a != NULL; a = a->next) {
        assert(a->type < LENGTH(color_palette));

        /* Round float positions to get the grid coordinates */
        const int x = (int)roundf(a->x);
        const int y = (int)roundf(a->y);

        /* Round mass to get the circle radius */
        const int radius = (int)roundf(a->mass);

        const uint32_t color = color_palette[a->type];

        if (a->type == BODY_STATIC) {
            draw_circle(rend, x, y, radius, color);
            continue;
        }

        draw_circle_filled(rend, x, y, radius, color);

        /* Draw the velocity line */
        const float vel_scale = a->mass * 1.5f;
        const int vx = (int)roundf(a->x + (a->vel_x * vel_scale));
        const int vy = (int)roundf(a->y + (a->vel_y * vel_scale));
        set_render_color(rend, 0x0000FF);
        SDL_RenderDrawLine(rend, x, y, vx, vy);

        /* Draw line between centers, if the bodies are close enough */
        for (Body* b = bodies; b != NULL; b = b->next) {
            if (a == b)
                continue;

            float dx       = b->x - x;
            float dy       = b->y - y;
            float distance = sqrtf(dx * dx + dy * dy);

            /* Only draw line if the bodies are close enough */
            if (distance > (radius + b->mass) * 3.f)
                continue;

            const int bx = (int)roundf(b->x);
            const int by = (int)roundf(b->y);

            set_render_color(rend, 0xFF0000);
            SDL_RenderDrawLine(rend, x, y, bx, by);
        }
    }
}

static void free_bodies(void) {
    Body* body = bodies;
    while (body != NULL) {
        Body* aux = body->next;
        free(body);
        body = aux;
    }
    bodies = NULL;
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
    bool clear_bodies = false;
    bool running      = true;
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
                        case SDL_SCANCODE_C:
                            clear_bodies = true;
                            break;
                        case SDL_SCANCODE_1:
                            current_mass -= CURRENT_MASS_STEP;
                            break;
                        case SDL_SCANCODE_2:
                            current_mass += CURRENT_MASS_STEP;
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
                case SDL_MOUSEWHEEL:
                    if (sdl_event.wheel.type != SDL_MOUSEWHEEL)
                        break;

                    /* Increase or decrease current mass with mouse wheel */
                    if (sdl_event.wheel.y > 0)
                        current_mass += CURRENT_MASS_STEP;
                    else
                        current_mass -= CURRENT_MASS_STEP;

                    break; /* End SDL_MOUSEWHEEL case */
                default:
                    break;
            } /* End event.type switch */
        }     /* End PollEvent while */

        /* If we pressed 'C' */
        if (clear_bodies) {
            free_bodies();
            clear_bodies = false;
            continue;
        }

        /* Make sure the global variables are within bounds */
        if (current_mass < 1.f)
            current_mass = 1.f;

        /* Clear window */
        set_render_color(sdl_renderer, 0x000000);
        SDL_RenderClear(sdl_renderer);

        /* Render the valid bodies before calculating new velocities, that way
         * the lines are accurate. */
        render_bodies(sdl_renderer);

        /* Calculate velocities of all bodies in case they are colliding */
        apply_bounces();

        /* Apply the velocity of each body */
        move_bodies();

        /* Send to renderer and delay depending on FPS */
        SDL_RenderPresent(sdl_renderer);
        SDL_Delay(1000 / FPS);
    }

    /* Free our linked list of bodies */
    free_bodies();

    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();

    return 0;
}

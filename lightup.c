/*
 * lightup.c: Implementation of the Nikoli game 'Light Up'.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

/* --- Constants, structure definitions, etc. --- */

#define PREFERRED_TILE_SIZE 32
#define TILE_SIZE       (ds->tilesize)
#define BORDER          (TILE_SIZE / 2)
#define TILE_RADIUS     (ds->crad)

#define COORD(x)  ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILE_SIZE) / TILE_SIZE - 1 )

#define FLASH_TIME 0.30F

enum {
    COL_BACKGROUND,
    COL_GRID,
    COL_BLACK,			       /* black */
    COL_LIGHT,			       /* white */
    COL_LIT,			       /* yellow */
    COL_ERROR,			       /* red */
    COL_CURSOR,
    NCOLOURS
};

enum { SYMM_NONE, SYMM_REF2, SYMM_ROT2, SYMM_REF4, SYMM_ROT4, SYMM_MAX };

struct game_params {
    int w, h;
    int blackpc;        /* %age of black squares */
    int symm;
    int recurse;
};

#define F_BLACK         1

/* flags for black squares */
#define F_NUMBERED      2       /* it has a number attached */
#define F_NUMBERUSED    4       /* this number was useful for solving */

/* flags for non-black squares */
#define F_IMPOSSIBLE    8       /* can't put a light here */
#define F_LIGHT         16

#define F_MARK          32

struct game_state {
    int w, h, nlights;
    int *lights;        /* For black squares, (optionally) the number
                           of surrounding lights. For non-black squares,
                           the number of times it's lit. size h*w*/
    unsigned int *flags;        /* size h*w */
    int completed, used_solve;
};

#define GRID(gs,grid,x,y) (gs->grid[(y)*((gs)->w) + (x)])

/* A ll_data holds information about which lights would be lit by
 * a particular grid location's light (or conversely, which locations
 * could light a specific other location). */
/* most things should consider this struct opaque. */
typedef struct {
    int ox,oy;
    int minx, maxx, miny, maxy;
    int include_origin;
} ll_data;

/* Macro that executes 'block' once per light in lld, including
 * the origin if include_origin is specified. 'block' can use
 * lx and ly as the coords. */
#define FOREACHLIT(lld,block) do {                              \
  int lx,ly;                                                    \
  ly = (lld)->oy;                                               \
  for (lx = (lld)->minx; lx <= (lld)->maxx; lx++) {             \
    if (lx == (lld)->ox) continue;                              \
    block                                                       \
  }                                                             \
  lx = (lld)->ox;                                               \
  for (ly = (lld)->miny; ly <= (lld)->maxy; ly++) {             \
    if (!(lld)->include_origin && ly == (lld)->oy) continue;    \
    block                                                       \
  }                                                             \
} while(0)


typedef struct {
    struct { int x, y; unsigned int f; } points[4];
    int npoints;
} surrounds;

/* Fills in (doesn't allocate) a surrounds structure with the grid locations
 * around a given square, taking account of the edges. */
static void get_surrounds(game_state *state, int ox, int oy, surrounds *s)
{
    assert(ox >= 0 && ox < state->w && oy >= 0 && oy < state->h);
    s->npoints = 0;
#define ADDPOINT(cond,nx,ny) do {\
    if (cond) { \
        s->points[s->npoints].x = (nx); \
        s->points[s->npoints].y = (ny); \
        s->points[s->npoints].f = 0; \
        s->npoints++; \
    } } while(0)
    ADDPOINT(ox > 0,            ox-1, oy);
    ADDPOINT(ox < (state->w-1), ox+1, oy);
    ADDPOINT(oy > 0,            ox,   oy-1);
    ADDPOINT(oy < (state->h-1), ox,   oy+1);
}

/* --- Game parameter functions --- */

#define DEFAULT_PRESET 0

const struct game_params lightup_presets[] = {
    { 7, 7, 20, SYMM_ROT4, 0 },
    { 7, 7, 20, SYMM_ROT4, 1 },
    { 10, 10, 20, SYMM_ROT2, 0 },
    { 10, 10, 20, SYMM_ROT2, 1 },
#ifdef SLOW_SYSTEM
    { 12, 12, 20, SYMM_ROT2, 0 },
    { 12, 12, 20, SYMM_ROT2, 1 }
#else
    { 14, 14, 20, SYMM_ROT2, 0 },
    { 14, 14, 20, SYMM_ROT2, 1 }
#endif
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);
    *ret = lightup_presets[DEFAULT_PRESET];

    return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char buf[80];

    if (i < 0 || i >= lenof(lightup_presets))
        return FALSE;

    ret = default_params();
    *ret = lightup_presets[i];
    *params = ret;

    sprintf(buf, "%dx%d %s",
            ret->w, ret->h, ret->recurse ? "hard" : "easy");
    *name = dupstr(buf);

    return TRUE;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

#define EATNUM(x) do { \
    (x) = atoi(string); \
    while (*string && isdigit((unsigned char)*string)) string++; \
} while(0)

static void decode_params(game_params *params, char const *string)
{
    EATNUM(params->w);
    if (*string == 'x') {
        string++;
        EATNUM(params->h);
    }
    if (*string == 'b') {
        string++;
        EATNUM(params->blackpc);
    }
    if (*string == 's') {
        string++;
        EATNUM(params->symm);
    }
    params->recurse = 0;
    if (*string == 'r') {
        params->recurse = 1;
        string++;
    }
}

static char *encode_params(game_params *params, int full)
{
    char buf[80];

    if (full) {
        sprintf(buf, "%dx%db%ds%d%s",
                params->w, params->h, params->blackpc,
                params->symm,
                params->recurse ? "r" : "");
    } else {
        sprintf(buf, "%dx%d", params->w, params->h);
    }
    return dupstr(buf);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(6, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].sval = dupstr(buf);
    ret[1].ival = 0;

    ret[2].name = "%age of black squares";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->blackpc);
    ret[2].sval = dupstr(buf);
    ret[2].ival = 0;

    ret[3].name = "Symmetry";
    ret[3].type = C_CHOICES;
    ret[3].sval = ":None"
                  ":2-way mirror:2-way rotational"
                  ":4-way mirror:4-way rotational";
    ret[3].ival = params->symm;

    ret[4].name = "Difficulty";
    ret[4].type = C_CHOICES;
    ret[4].sval = ":Easy:Hard";
    ret[4].ival = params->recurse;

    ret[5].name = NULL;
    ret[5].type = C_END;
    ret[5].sval = NULL;
    ret[5].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w =       atoi(cfg[0].sval);
    ret->h =       atoi(cfg[1].sval);
    ret->blackpc = atoi(cfg[2].sval);
    ret->symm =    cfg[3].ival;
    ret->recurse = cfg[4].ival;

    return ret;
}

static char *validate_params(game_params *params, int full)
{
    if (params->w < 2 || params->h < 2)
        return "Width and height must be at least 2";
    if (full) {
        if (params->blackpc < 5 || params->blackpc > 100)
            return "Percentage of black squares must be between 5% and 100%";
        if (params->w != params->h) {
            if (params->symm == SYMM_ROT4)
                return "4-fold symmetry is only available with square grids";
        }
        if (params->symm < 0 || params->symm >= SYMM_MAX)
          return "Unknown symmetry type";
    }
    return NULL;
}

/* --- Game state construction/freeing helper functions --- */

static game_state *new_state(game_params *params)
{
    game_state *ret = snew(game_state);

    ret->w = params->w;
    ret->h = params->h;
    ret->lights = snewn(ret->w * ret->h, int);
    ret->nlights = 0;
    memset(ret->lights, 0, ret->w * ret->h * sizeof(int));
    ret->flags = snewn(ret->w * ret->h, unsigned int);
    memset(ret->flags, 0, ret->w * ret->h * sizeof(unsigned int));
    ret->completed = ret->used_solve = 0;
    return ret;
}

static game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;

    ret->lights = snewn(ret->w * ret->h, int);
    memcpy(ret->lights, state->lights, ret->w * ret->h * sizeof(int));
    ret->nlights = state->nlights;

    ret->flags = snewn(ret->w * ret->h, unsigned int);
    memcpy(ret->flags, state->flags, ret->w * ret->h * sizeof(unsigned int));

    ret->completed = state->completed;
    ret->used_solve = state->used_solve;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->lights);
    sfree(state->flags);
    sfree(state);
}

#ifdef DIAGNOSTICS
static void debug_state(game_state *state)
{
    int x, y;
    char c = '?';

    for (y = 0; y < state->h; y++) {
        for (x = 0; x < state->w; x++) {
            c = '.';
            if (GRID(state, flags, x, y) & F_BLACK) {
                if (GRID(state, flags, x, y) & F_NUMBERED)
                    c = GRID(state, lights, x, y) + '0';
                else
                    c = '#';
            } else {
                if (GRID(state, flags, x, y) & F_LIGHT)
                    c = 'O';
                else if (GRID(state, flags, x, y) & F_IMPOSSIBLE)
                    c = 'X';
            }
            printf("%c", (int)c);
        }
        printf("     ");
        for (x = 0; x < state->w; x++) {
            if (GRID(state, flags, x, y) & F_BLACK)
                c = '#';
            else {
                c = (GRID(state, flags, x, y) & F_LIGHT) ? 'A' : 'a';
                c += GRID(state, lights, x, y);
            }
            printf("%c", (int)c);
        }
        printf("\n");
    }
    printf("\n");
}
#endif

/* --- Game completion test routines. --- */

/* These are split up because occasionally functions are only
 * interested in one particular aspect. */

/* Returns non-zero if all grid spaces are lit. */
static int grid_lit(game_state *state)
{
    int x, y;

    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            if (GRID(state,flags,x,y) & F_BLACK) continue;
            if (GRID(state,lights,x,y) == 0)
                return 0;
        }
    }
    return 1;
}

/* Returns non-zero if any lights are lit by other lights. */
static int grid_overlap(game_state *state)
{
    int x, y;

    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            if (!(GRID(state, flags, x, y) & F_LIGHT)) continue;
            if (GRID(state, lights, x, y) > 1)
                return 1;
        }
    }
    return 0;
}

static int number_wrong(game_state *state, int x, int y)
{
    surrounds s;
    int i, n, empty, lights = GRID(state, lights, x, y);

    /*
     * This function computes the display hint for a number: we
     * turn the number red if it is definitely wrong. This means
     * that either
     * 
     *  (a) it has too many lights around it, or
     * 	(b) it would have too few lights around it even if all the
     * 	    plausible squares (not black, lit or F_IMPOSSIBLE) were
     * 	    filled with lights.
     */

    assert(GRID(state, flags, x, y) & F_NUMBERED);
    get_surrounds(state, x, y, &s);

    empty = n = 0;
    for (i = 0; i < s.npoints; i++) {
	if (GRID(state,flags,s.points[i].x,s.points[i].y) & F_LIGHT) {
	    n++;
	    continue;
	}
	if (GRID(state,flags,s.points[i].x,s.points[i].y) & F_BLACK)
	    continue;
	if (GRID(state,flags,s.points[i].x,s.points[i].y) & F_IMPOSSIBLE)
	    continue;
	if (GRID(state,lights,s.points[i].x,s.points[i].y))
	    continue;
	empty++;
    }
    return (n > lights || (n + empty < lights));
}

static int number_correct(game_state *state, int x, int y)
{
    surrounds s;
    int n = 0, i, lights = GRID(state, lights, x, y);

    assert(GRID(state, flags, x, y) & F_NUMBERED);
    get_surrounds(state, x, y, &s);
    for (i = 0; i < s.npoints; i++) {
        if (GRID(state,flags,s.points[i].x,s.points[i].y) & F_LIGHT)
            n++;
    }
    return (n == lights) ? 1 : 0;
}

/* Returns non-zero if any numbers add up incorrectly. */
static int grid_addsup(game_state *state)
{
    int x, y;

    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            if (!(GRID(state, flags, x, y) & F_NUMBERED)) continue;
            if (!number_correct(state, x, y)) return 0;
        }
    }
    return 1;
}

static int grid_correct(game_state *state)
{
    if (grid_lit(state) &&
        !grid_overlap(state) &&
        grid_addsup(state)) return 1;
    return 0;
}

/* --- Board initial setup (blacks, lights, numbers) --- */

static void clean_board(game_state *state, int leave_blacks)
{
    int x,y;
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            if (leave_blacks)
                GRID(state, flags, x, y) &= F_BLACK;
            else
                GRID(state, flags, x, y) = 0;
            GRID(state, lights, x, y) = 0;
        }
    }
    state->nlights = 0;
}

static void set_blacks(game_state *state, game_params *params, random_state *rs)
{
    int x, y, degree = 0, rotate = 0, nblack;
    int rh, rw, i;
    int wodd = (state->w % 2) ? 1 : 0;
    int hodd = (state->h % 2) ? 1 : 0;
    int xs[4], ys[4];

    switch (params->symm) {
    case SYMM_NONE: degree = 1; rotate = 0; break;
    case SYMM_ROT2: degree = 2; rotate = 1; break;
    case SYMM_REF2: degree = 2; rotate = 0; break;
    case SYMM_ROT4: degree = 4; rotate = 1; break;
    case SYMM_REF4: degree = 4; rotate = 0; break;
    default: assert(!"Unknown symmetry type");
    }
    if (params->symm == SYMM_ROT4 && (state->h != state->w))
        assert(!"4-fold symmetry unavailable without square grid");

    if (degree == 4) {
        rw = state->w/2;
        rh = state->h/2;
        if (!rotate) rw += wodd; /* ... but see below. */
        rh += hodd;
    } else if (degree == 2) {
        rw = state->w;
        rh = state->h/2;
        rh += hodd;
    } else {
        rw = state->w;
        rh = state->h;
    }

    /* clear, then randomise, required region. */
    clean_board(state, 0);
    nblack = (rw * rh * params->blackpc) / 100;
    for (i = 0; i < nblack; i++) {
        do {
            x = random_upto(rs,rw);
            y = random_upto(rs,rh);
        } while (GRID(state,flags,x,y) & F_BLACK);
        GRID(state, flags, x, y) |= F_BLACK;
    }

    /* Copy required region. */
    if (params->symm == SYMM_NONE) return;

    for (x = 0; x < rw; x++) {
        for (y = 0; y < rh; y++) {
            if (degree == 4) {
                xs[0] = x;
                ys[0] = y;
                xs[1] = state->w - 1 - (rotate ? y : x);
                ys[1] = rotate ? x : y;
                xs[2] = rotate ? (state->w - 1 - x) : x;
                ys[2] = state->h - 1 - y;
                xs[3] = rotate ? y : (state->w - 1 - x);
                ys[3] = state->h - 1 - (rotate ? x : y);
            } else {
                xs[0] = x;
                ys[0] = y;
                xs[1] = rotate ? (state->w - 1 - x) : x;
                ys[1] = state->h - 1 - y;
            }
            for (i = 1; i < degree; i++) {
                GRID(state, flags, xs[i], ys[i]) =
                    GRID(state, flags, xs[0], ys[0]);
            }
        }
    }
    /* SYMM_ROT4 misses the middle square above; fix that here. */
    if (degree == 4 && rotate && wodd &&
        (random_upto(rs,100) <= (unsigned int)params->blackpc))
        GRID(state,flags,
             state->w/2 + wodd - 1, state->h/2 + hodd - 1) |= F_BLACK;

#ifdef DIAGNOSTICS
    debug_state(state);
#endif
}

/* Fills in (does not allocate) a ll_data with all the tiles that would
 * be illuminated by a light at point (ox,oy). If origin=1 then the
 * origin is included in this list. */
static void list_lights(game_state *state, int ox, int oy, int origin,
                        ll_data *lld)
{
    int x,y;

    memset(lld, 0, sizeof(lld));
    lld->ox = lld->minx = lld->maxx = ox;
    lld->oy = lld->miny = lld->maxy = oy;
    lld->include_origin = origin;

    y = oy;
    for (x = ox-1; x >= 0; x--) {
        if (GRID(state, flags, x, y) & F_BLACK) break;
        if (x < lld->minx) lld->minx = x;
    }
    for (x = ox+1; x < state->w; x++) {
        if (GRID(state, flags, x, y) & F_BLACK) break;
        if (x > lld->maxx) lld->maxx = x;
    }

    x = ox;
    for (y = oy-1; y >= 0; y--) {
        if (GRID(state, flags, x, y) & F_BLACK) break;
        if (y < lld->miny) lld->miny = y;
    }
    for (y = oy+1; y < state->h; y++) {
        if (GRID(state, flags, x, y) & F_BLACK) break;
        if (y > lld->maxy) lld->maxy = y;
    }
}

/* Makes sure a light is the given state, editing the lights table to suit the
 * new state if necessary. */
static void set_light(game_state *state, int ox, int oy, int on)
{
    ll_data lld;
    int diff = 0;

    assert(!(GRID(state,flags,ox,oy) & F_BLACK));

    if (!on && GRID(state,flags,ox,oy) & F_LIGHT) {
        diff = -1;
        GRID(state,flags,ox,oy) &= ~F_LIGHT;
        state->nlights--;
    } else if (on && !(GRID(state,flags,ox,oy) & F_LIGHT)) {
        diff = 1;
        GRID(state,flags,ox,oy) |= F_LIGHT;
        state->nlights++;
    }

    if (diff != 0) {
        list_lights(state,ox,oy,1,&lld);
        FOREACHLIT(&lld, GRID(state,lights,lx,ly) += diff; );
    }
}

/* Returns 1 if removing a light at (x,y) would cause a square to go dark. */
static int check_dark(game_state *state, int x, int y)
{
    ll_data lld;

    list_lights(state, x, y, 1, &lld);
    FOREACHLIT(&lld, if (GRID(state,lights,lx,ly) == 1) { return 1; } );
    return 0;
}

/* Sets up an initial random correct position (i.e. every
 * space lit, and no lights lit by other lights) by filling the
 * grid with lights and then removing lights one by one at random. */
static void place_lights(game_state *state, random_state *rs)
{
    int i, x, y, n, *numindices, wh = state->w*state->h;
    ll_data lld;

    numindices = snewn(wh, int);
    for (i = 0; i < wh; i++) numindices[i] = i;
    shuffle(numindices, wh, sizeof(*numindices), rs);

    /* Place a light on all grid squares without lights. */
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            GRID(state, flags, x, y) &= ~F_MARK; /* we use this later. */
            if (GRID(state, flags, x, y) & F_BLACK) continue;
            set_light(state, x, y, 1);
        }
    }

    for (i = 0; i < wh; i++) {
        y = numindices[i] / state->w;
        x = numindices[i] % state->w;
        if (!(GRID(state, flags, x, y) & F_LIGHT)) continue;
        if (GRID(state, flags, x, y) & F_MARK) continue;
        list_lights(state, x, y, 0, &lld);

        /* If we're not lighting any lights ourself, don't remove anything. */
        n = 0;
        FOREACHLIT(&lld, if (GRID(state,flags,lx,ly) & F_LIGHT) { n += 1; } );
        if (n == 0) continue;

        /* Check whether removing lights we're lighting would cause anything
         * to go dark. */
        n = 0;
        FOREACHLIT(&lld, if (GRID(state,flags,lx,ly) & F_LIGHT) { n += check_dark(state,lx,ly); } );
        if (n == 0) {
            /* No, it wouldn't, so we can remove them all. */
            FOREACHLIT(&lld, set_light(state,lx,ly, 0); );
            GRID(state,flags,x,y) |= F_MARK;
        }

        if (!grid_overlap(state)) {
            sfree(numindices);
            return; /* we're done. */
        }
        assert(grid_lit(state));
    }
    /* if we got here, we've somehow removed all our lights and still have overlaps. */
    assert(!"Shouldn't get here!");
}

/* Fills in all black squares with numbers of adjacent lights. */
static void place_numbers(game_state *state)
{
    int x, y, i, n;
    surrounds s;

    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            if (!(GRID(state,flags,x,y) & F_BLACK)) continue;
            get_surrounds(state, x, y, &s);
            n = 0;
            for (i = 0; i < s.npoints; i++) {
                if (GRID(state,flags,s.points[i].x, s.points[i].y) & F_LIGHT)
                    n++;
            }
            GRID(state,flags,x,y) |= F_NUMBERED;
            GRID(state,lights,x,y) = n;
        }
    }
}

/* --- Actual solver, with helper subroutines. --- */

static void tsl_callback(game_state *state,
                         int lx, int ly, int *x, int *y, int *n)
{
    if (GRID(state,flags,lx,ly) & F_IMPOSSIBLE) return;
    if (GRID(state,lights,lx,ly) > 0) return;
    *x = lx; *y = ly; (*n)++;
}

static int try_solve_light(game_state *state, int ox, int oy,
                           unsigned int flags, int lights)
{
    ll_data lld;
    int sx,sy,n = 0;

    if (lights > 0) return 0;
    if (flags & F_BLACK) return 0;

    /* We have an unlit square; count how many ways there are left to
     * place a light that lights us (including this square); if only
     * one, we must put a light there. Squares that could light us
     * are, of course, the same as the squares we would light... */
    list_lights(state, ox, oy, 1, &lld);
    FOREACHLIT(&lld, { tsl_callback(state, lx, ly, &sx, &sy, &n); });
    if (n == 1) {
        set_light(state, sx, sy, 1);
#ifdef SOLVE_DIAGNOSTICS
        printf("(%d,%d) can only be lit from (%d,%d); setting to LIGHT\n",
               ox,oy,sx,sy);
#endif
        return 1;
    }

    return 0;
}

static int could_place_light(unsigned int flags, int lights)
{
    if (flags & (F_BLACK | F_IMPOSSIBLE)) return 0;
    return (lights > 0) ? 0 : 1;
}

/* For a given number square, determine whether we have enough info
 * to unambiguously place its lights. */
static int try_solve_number(game_state *state, int nx, int ny,
                            unsigned int nflags, int nlights)
{
    surrounds s;
    int x, y, nl, ns, i, ret = 0, lights;
    unsigned int flags;

    if (!(nflags & F_NUMBERED)) return 0;
    nl = nlights;
    get_surrounds(state,nx,ny,&s);
    ns = s.npoints;

    /* nl is no. of lights we need to place, ns is no. of spaces we
     * have to place them in. Try and narrow these down, and mark
     * points we can ignore later. */
    for (i = 0; i < s.npoints; i++) {
        x = s.points[i].x; y = s.points[i].y;
        flags = GRID(state,flags,x,y);
        lights = GRID(state,lights,x,y);
        if (flags & F_LIGHT) {
            /* light here already; one less light for one less place. */
            nl--; ns--;
            s.points[i].f |= F_MARK;
        } else if (!could_place_light(flags, lights)) {
            ns--;
            s.points[i].f |= F_MARK;
        }
    }
    if (ns == 0) return 0; /* nowhere to put anything. */
    if (nl == 0) {
        /* we have placed all lights we need to around here; all remaining
         * surrounds are therefore IMPOSSIBLE. */
#ifdef SOLVE_DIAGNOSTICS
        printf("Setting remaining surrounds to (%d,%d) IMPOSSIBLE.\n",
               nx,ny);
#endif
        GRID(state,flags,nx,ny) |= F_NUMBERUSED;
        for (i = 0; i < s.npoints; i++) {
            if (!(s.points[i].f & F_MARK)) {
                GRID(state,flags,s.points[i].x,s.points[i].y) |= F_IMPOSSIBLE;
                ret = 1;
            }
        }
    } else if (nl == ns) {
        /* we have as many lights to place as spaces; fill them all. */
#ifdef SOLVE_DIAGNOSTICS
        printf("Setting all remaining surrounds to (%d,%d) LIGHT.\n",
               nx,ny);
#endif
        GRID(state,flags,nx,ny) |= F_NUMBERUSED;
        for (i = 0; i < s.npoints; i++) {
            if (!(s.points[i].f & F_MARK)) {
                set_light(state, s.points[i].x,s.points[i].y, 1);
                ret = 1;
            }
        }
    }
    return ret;
}

static int solve_sub(game_state *state,
                     int forceunique, int maxrecurse, int depth,
                     int *maxdepth)
{
    unsigned int flags;
    int x, y, didstuff, ncanplace, lights;
    int bestx, besty, n, bestn, copy_soluble, self_soluble, ret;
    game_state *scopy;
    ll_data lld;

#ifdef SOLVE_DIAGNOSTICS
    printf("solve_sub: depth = %d\n", depth);
#endif
    if (maxdepth && *maxdepth < depth) *maxdepth = depth;

    while (1) {
        if (grid_overlap(state)) {
            /* Our own solver, from scratch, should never cause this to happen
             * (assuming a soluble grid). However, if we're trying to solve
             * from a half-completed *incorrect* grid this might occur; we
             * just return the 'no solutions' code in this case. */
            return 0;
        }

        if (grid_correct(state)) return 1;

        ncanplace = 0;
        didstuff = 0;
        /* These 2 loops, and the functions they call, are the critical loops
         * for timing; any optimisations should look here first. */
        for (x = 0; x < state->w; x++) {
            for (y = 0; y < state->h; y++) {
                flags = GRID(state,flags,x,y);
                lights = GRID(state,lights,x,y);
                ncanplace += could_place_light(flags, lights);

                if (try_solve_light(state, x, y, flags, lights)) didstuff = 1;
                if (try_solve_number(state, x, y, flags, lights)) didstuff = 1;
            }
        }
        if (didstuff) continue;
        if (!ncanplace) return 0; /* nowhere to put a light, puzzle in unsoluble. */

        /* We now have to make a guess; we have places to put lights but
         * no definite idea about where they can go. */
        if (depth >= maxrecurse) return -1; /* mustn't delve any deeper. */

        /* Of all the squares that we could place a light, pick the one
         * that would light the most currently unlit squares. */
        /* This heuristic was just plucked from the air; there may well be
         * a more efficient way of choosing a square to flip to minimise
         * recursion. */
        bestn = 0;
        bestx = besty = -1; /* suyb */
        for (x = 0; x < state->w; x++) {
            for (y = 0; y < state->h; y++) {
                flags = GRID(state,flags,x,y);
                lights = GRID(state,lights,x,y);
                if (!could_place_light(flags, lights)) continue;

                n = 0;
                list_lights(state, x, y, 1, &lld);
                FOREACHLIT(&lld, { if (GRID(state,lights,lx,ly) == 0) n++; });
                if (n > bestn) {
                    bestn = n; bestx = x; besty = y;
                }
            }
        }
        assert(bestn > 0);
	assert(bestx >= 0 && besty >= 0);

        /* Now we've chosen a plausible (x,y), try to solve it once as 'lit'
         * and once as 'impossible'; we need to make one copy to do this. */

        scopy = dup_game(state);
        GRID(state,flags,bestx,besty) |= F_IMPOSSIBLE;
        self_soluble = solve_sub(state, forceunique, maxrecurse,
                                 depth+1, maxdepth);

        if (!forceunique && self_soluble > 0) {
            /* we didn't care about finding all solutions, and we just
             * found one; return with it immediately. */
            free_game(scopy);
            return self_soluble;
        }

        set_light(scopy, bestx, besty, 1);
        copy_soluble = solve_sub(scopy, forceunique, maxrecurse,
                                 depth+1, maxdepth);

        /* If we wanted a unique solution but we hit our recursion limit
         * (on either branch) then we have to assume we didn't find possible
         * extra solutions, and return 'not soluble'. */
        if (forceunique &&
            ((copy_soluble < 0) || (self_soluble < 0))) {
            ret = -1;
        /* Make sure that whether or not it was self or copy (or both) that
         * were soluble, that we return a solved state in self. */
        } else if (copy_soluble <= 0) {
            /* copy wasn't soluble; keep self state and return that result. */
            ret = self_soluble;
        } else if (self_soluble <= 0) {
            /* copy solved and we didn't, so copy in copy's (now solved)
             * flags and light state. */
            memcpy(state->lights, scopy->lights,
                   scopy->w * scopy->h * sizeof(int));
            memcpy(state->flags, scopy->flags,
                   scopy->w * scopy->h * sizeof(unsigned int));
            ret = copy_soluble;
        } else {
            ret = copy_soluble + self_soluble;
        }
        free_game(scopy);
        return ret;
    }
}

#define MAXRECURSE 5

/* Fills in the (possibly partially-complete) game_state as far as it can,
 * returning the number of possible solutions. If it returns >0 then the
 * game_state will be in a solved state, but you won't know which one. */
static int dosolve(game_state *state,
                   int allowguess, int forceunique, int *maxdepth)
{
    int x, y, nsol;

    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            GRID(state,flags,x,y) &= ~F_NUMBERUSED;
        }
    }
    nsol = solve_sub(state, forceunique,
                     allowguess ? MAXRECURSE : 0, 0, maxdepth);
    return nsol;
}

static int strip_unused_nums(game_state *state)
{
    int x,y,n=0;
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            if ((GRID(state,flags,x,y) & F_NUMBERED) &&
                !(GRID(state,flags,x,y) & F_NUMBERUSED)) {
                GRID(state,flags,x,y) &= ~F_NUMBERED;
                GRID(state,lights,x,y) = 0;
                n++;
            }
        }
    }
    return n;
}

static void unplace_lights(game_state *state)
{
    int x,y;
    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            if (GRID(state,flags,x,y) & F_LIGHT)
                set_light(state,x,y,0);
            GRID(state,flags,x,y) &= ~F_IMPOSSIBLE;
            GRID(state,flags,x,y) &= ~F_NUMBERUSED;
        }
    }
}

static int puzzle_is_good(game_state *state, game_params *params, int *mdepth)
{
    int nsol;

    *mdepth = 0;
    unplace_lights(state);

#ifdef DIAGNOSTICS
    debug_state(state);
#endif

    nsol = dosolve(state, params->recurse, TRUE, mdepth);
    /* if we wanted an easy puzzle, make sure we didn't need recursion. */
    if (!params->recurse && *mdepth > 0) {
#ifdef DIAGNOSTICS
        printf("Ignoring recursive puzzle.\n");
#endif
        return 0;
    }

#ifdef DIAGNOSTICS
    printf("%d solutions found.\n", nsol);
#endif
    if (nsol <= 0) return 0;
    if (nsol > 1) return 0;
    return 1;
}

/* --- New game creation and user input code. --- */

/* The basic algorithm here is to generate the most complex grid possible
 * while honouring two restrictions:
 *
 *  * we require a unique solution, and
 *  * either we require solubility with no recursion (!params->recurse)
 *  * or we require some recursion. (params->recurse).
 *
 * The solver helpfully keeps track of the numbers it needed to use to
 * get its solution, so we use that to remove an initial set of numbers
 * and check we still satsify our requirements (on uniqueness and
 * non-recursiveness, if applicable; we don't check explicit recursiveness
 * until the end).
 *
 * Then we try to remove all numbers in a random order, and see if we
 * still satisfy requirements (putting them back if we didn't).
 *
 * Removing numbers will always, in general terms, make a puzzle require
 * more recursion but it may also mean a puzzle becomes non-unique.
 *
 * Once we're done, if we wanted a recursive puzzle but the most difficult
 * puzzle we could come up with was non-recursive, we give up and try a new
 * grid. */

#define MAX_GRIDGEN_TRIES 20

static char *new_game_desc(game_params *params, random_state *rs,
			   char **aux, int interactive)
{
    game_state *news = new_state(params), *copys;
    int nsol, i, run, x, y, wh = params->w*params->h, num, mdepth;
    char *ret, *p;
    int *numindices;

    /* Construct a shuffled list of grid positions; we only
     * do this once, because if it gets used more than once it'll
     * be on a different grid layout. */
    numindices = snewn(wh, int);
    for (i = 0; i < wh; i++) numindices[i] = i;
    shuffle(numindices, wh, sizeof(*numindices), rs);

    while (1) {
        for (i = 0; i < MAX_GRIDGEN_TRIES; i++) {
            set_blacks(news, params, rs); /* also cleans board. */

            /* set up lights and then the numbers, and remove the lights */
            place_lights(news, rs);
            debug(("Generating initial grid.\n"));
            place_numbers(news);
            if (!puzzle_is_good(news, params, &mdepth)) continue;

            /* Take a copy, remove numbers we didn't use and check there's
             * still a unique solution; if so, use the copy subsequently. */
            copys = dup_game(news);
            nsol = strip_unused_nums(copys);
            debug(("Stripped %d unused numbers.\n", nsol));
            if (!puzzle_is_good(copys, params, &mdepth)) {
                debug(("Stripped grid is not good, reverting.\n"));
                free_game(copys);
            } else {
                free_game(news);
                news = copys;
            }

            /* Go through grid removing numbers at random one-by-one and
             * trying to solve again; if it ceases to be good put the number back. */
            for (i = 0; i < wh; i++) {
                y = numindices[i] / params->w;
                x = numindices[i] % params->w;
                if (!(GRID(news, flags, x, y) & F_NUMBERED)) continue;
                num = GRID(news, lights, x, y);
                GRID(news, lights, x, y) = 0;
                GRID(news, flags, x, y) &= ~F_NUMBERED;
                if (!puzzle_is_good(news, params, &mdepth)) {
                    GRID(news, lights, x, y) = num;
                    GRID(news, flags, x, y) |= F_NUMBERED;
                } else
                    debug(("Removed (%d,%d) still soluble.\n", x, y));
            }
	    /* Get a good value of mdepth for the following test */
	    i = puzzle_is_good(news, params, &mdepth);
	    assert(i);
            if (params->recurse && mdepth == 0) {
                debug(("Maximum-difficulty puzzle still not recursive, skipping.\n"));
                continue;
            }

            goto goodpuzzle;
        }
        /* Couldn't generate a good puzzle in however many goes. Ramp up the
         * %age of black squares (if we didn't already have lots; in which case
         * why couldn't we generate a puzzle?) and try again. */
        if (params->blackpc < 90) params->blackpc += 5;
#ifdef DIAGNOSTICS
        printf("New black layout %d%%.\n", params->blackpc);
#endif
    }
goodpuzzle:
    /* Game is encoded as a long string one character per square;
     * 'S' is a space
     * 'B' is a black square with no number
     * '0', '1', '2', '3', '4' is a black square with a number. */
    ret = snewn((params->w * params->h) + 1, char);
    p = ret;
    run = 0;
    for (y = 0; y < params->h; y++) {
	for (x = 0; x < params->w; x++) {
            if (GRID(news,flags,x,y) & F_BLACK) {
		if (run) {
		    *p++ = ('a'-1) + run;
		    run = 0;
		}
                if (GRID(news,flags,x,y) & F_NUMBERED)
                    *p++ = '0' + GRID(news,lights,x,y);
                else
                    *p++ = 'B';
            } else {
		if (run == 26) {
		    *p++ = ('a'-1) + run;
		    run = 0;
		}
		run++;
	    }
        }
    }
    if (run) {
	*p++ = ('a'-1) + run;
	run = 0;
    }
    *p = '\0';
    assert(p - ret <= params->w * params->h);
    free_game(news);
    sfree(numindices);

    return ret;
}

static char *validate_desc(game_params *params, char *desc)
{
    int i;
    for (i = 0; i < params->w*params->h; i++) {
        if (*desc >= '0' && *desc <= '4')
            /* OK */;
        else if (*desc == 'B')
            /* OK */;
        else if (*desc >= 'a' && *desc <= 'z')
            i += *desc - 'a';	       /* and the i++ will add another one */
        else if (!*desc)
            return "Game description shorter than expected";
        else
            return "Game description contained unexpected character";
        desc++;
    }
    if (*desc || i > params->w*params->h)
        return "Game description longer than expected";

    return NULL;
}

static game_state *new_game(midend *me, game_params *params, char *desc)
{
    game_state *ret = new_state(params);
    int x,y;
    int run = 0;

    for (y = 0; y < params->h; y++) {
	for (x = 0; x < params->w; x++) {
            char c = '\0';

	    if (run == 0) {
		c = *desc++;
		assert(c != 'S');
		if (c >= 'a' && c <= 'z')
		    run = c - 'a' + 1;
	    }

	    if (run > 0) {
		c = 'S';
		run--;
	    }

            switch (c) {
	      case '0': case '1': case '2': case '3': case '4':
                GRID(ret,flags,x,y) |= F_NUMBERED;
                GRID(ret,lights,x,y) = (c - '0');
                /* run-on... */

	      case 'B':
                GRID(ret,flags,x,y) |= F_BLACK;
                break;

	      case 'S':
		/* empty square */
                break;

	      default:
		assert(!"Malformed desc.");
		break;
            }
        }
    }
    if (*desc) assert(!"Over-long desc.");

    return ret;
}

static char *solve_game(game_state *state, game_state *currstate,
			char *aux, char **error)
{
    game_state *solved;
    char *move = NULL, buf[80];
    int movelen, movesize, x, y, len;
    unsigned int oldflags, solvedflags;

    /* We don't care here about non-unique puzzles; if the
     * user entered one themself then I doubt they care. */

    /* Try and solve from where we are now (for non-unique
     * puzzles this may produce a different answer). */
    solved = dup_game(currstate);
    if (dosolve(solved, 1, 0, NULL) > 0) goto solved;
    free_game(solved);

    /* That didn't work; try solving from the clean puzzle. */
    solved = dup_game(state);
    if (dosolve(solved, 1, 0, NULL) > 0) goto solved;
    *error = "Puzzle is not self-consistent.";
    goto done;

solved:
    movesize = 256;
    move = snewn(movesize, char);
    movelen = 0;
    move[movelen++] = 'S';
    move[movelen] = '\0';
    for (x = 0; x < currstate->w; x++) {
        for (y = 0; y < currstate->h; y++) {
            len = 0;
            oldflags = GRID(currstate, flags, x, y);
            solvedflags = GRID(solved, flags, x, y);
            if ((oldflags & F_LIGHT) != (solvedflags & F_LIGHT))
                len = sprintf(buf, ";L%d,%d", x, y);
            else if ((oldflags & F_IMPOSSIBLE) != (solvedflags & F_IMPOSSIBLE))
                len = sprintf(buf, ";I%d,%d", x, y);
            if (len) {
                if (movelen + len >= movesize) {
                    movesize = movelen + len + 256;
                    move = sresize(move, movesize, char);
                }
                strcpy(move + movelen, buf);
                movelen += len;
            }
        }
    }

done:
    free_game(solved);
    return move;
}

/* 'borrowed' from slant.c, mainly. I could have printed it one
 * character per cell (like debug_state) but that comes out tiny.
 * 'L' is used for 'light here' because 'O' looks too much like '0'
 * (black square with no surrounding lights). */
static char *game_text_format(game_state *state)
{
    int w = state->w, h = state->h, W = w+1, H = h+1;
    int x, y, len, lights;
    unsigned int flags;
    char *ret, *p;

    len = (h+H) * (w+W+1) + 1;
    ret = snewn(len, char);
    p = ret;

    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            *p++ = '+';
            if (x < w)
                *p++ = '-';
        }
        *p++ = '\n';
        if (y < h) {
            for (x = 0; x < W; x++) {
                *p++ = '|';
                if (x < w) {
                    /* actual interesting bit. */
                    flags = GRID(state, flags, x, y);
                    lights = GRID(state, lights, x, y);
                    if (flags & F_BLACK) {
                        if (flags & F_NUMBERED)
                            *p++ = '0' + lights;
                        else
                            *p++ = '#';
                    } else {
                        if (flags & F_LIGHT)
                            *p++ = 'L';
                        else if (flags & F_IMPOSSIBLE)
                            *p++ = 'x';
                        else if (lights > 0)
                            *p++ = '.';
                        else
                            *p++ = ' ';
                    }
                }
            }
            *p++ = '\n';
        }
    }
    *p++ = '\0';

    assert(p - ret == len);
    return ret;
}

struct game_ui {
    int cur_x, cur_y, cur_visible;
};

static game_ui *new_ui(game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->cur_x = ui->cur_y = ui->cur_visible = 0;
    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static char *encode_ui(game_ui *ui)
{
    /* nothing to encode. */
    return NULL;
}

static void decode_ui(game_ui *ui, char *encoding)
{
    /* nothing to decode. */
}

static void game_changed_state(game_ui *ui, game_state *oldstate,
                               game_state *newstate)
{
    if (newstate->completed)
        ui->cur_visible = 0;
}

#define DF_BLACK        1       /* black square */
#define DF_NUMBERED     2       /* black square with number */
#define DF_LIT          4       /* display (white) square lit up */
#define DF_LIGHT        8       /* display light in square */
#define DF_OVERLAP      16      /* display light as overlapped */
#define DF_CURSOR       32      /* display cursor */
#define DF_NUMBERWRONG  64      /* display black numbered square as error. */
#define DF_FLASH        128     /* background flash is on. */
#define DF_IMPOSSIBLE   256     /* display non-light little square */

struct game_drawstate {
    int tilesize, crad;
    int w, h;
    unsigned int *flags;         /* width * height */
    int started;
};


/* Believe it or not, this empty = "" hack is needed to get around a bug in
 * the prc-tools gcc when optimisation is turned on; before, it produced:
    lightup-sect.c: In function `interpret_move':
    lightup-sect.c:1416: internal error--unrecognizable insn:
    (insn 582 580 583 (set (reg:SI 134)
            (pc)) -1 (nil)
        (nil))
 */
static char *interpret_move(game_state *state, game_ui *ui, game_drawstate *ds,
			    int x, int y, int button)
{
    enum { NONE, FLIP_LIGHT, FLIP_IMPOSSIBLE } action = NONE;
    int cx = -1, cy = -1, cv = ui->cur_visible;
    unsigned int flags;
    char buf[80], *nullret, *empty = "", c;

    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
        ui->cur_visible = 0;
        cx = FROMCOORD(x);
        cy = FROMCOORD(y);
        action = (button == LEFT_BUTTON) ? FLIP_LIGHT : FLIP_IMPOSSIBLE;
    } else if (button == CURSOR_SELECT ||
               button == 'i' || button == 'I' ||
               button == ' ' || button == '\r' || button == '\n') {
        ui->cur_visible = 1;
        cx = ui->cur_x;
        cy = ui->cur_y;
        action = (button == 'i' || button == 'I') ?
            FLIP_IMPOSSIBLE : FLIP_LIGHT;
    } else if (button == CURSOR_UP || button == CURSOR_DOWN ||
               button == CURSOR_RIGHT || button == CURSOR_LEFT) {
        int dx = 0, dy = 0;
        switch (button) {
        case CURSOR_UP:         dy = -1; break;
        case CURSOR_DOWN:       dy = 1; break;
        case CURSOR_RIGHT:      dx = 1; break;
        case CURSOR_LEFT:       dx = -1; break;
        default: assert(!"shouldn't get here");
        }
        ui->cur_x += dx; ui->cur_y += dy;
        ui->cur_x = min(max(ui->cur_x, 0), state->w - 1);
        ui->cur_y = min(max(ui->cur_y, 0), state->h - 1);
        ui->cur_visible = 1;
    }

    /* Always redraw if the cursor is on, or if it's just been
     * removed. */
    if (ui->cur_visible) nullret = empty;
    else if (cv) nullret = empty;
    else nullret = NULL;

    switch (action) {
    case FLIP_LIGHT:
    case FLIP_IMPOSSIBLE:
        if (cx < 0 || cy < 0 || cx >= state->w || cy >= state->h)
            return nullret;
        flags = GRID(state, flags, cx, cy);
        if (flags & F_BLACK)
            return nullret;
        if (action == FLIP_LIGHT) {
            if (flags & F_IMPOSSIBLE) return nullret;
            c = 'L';
        } else {
            if (flags & F_LIGHT) return nullret;
            c = 'I';
        }
        sprintf(buf, "%c%d,%d", (int)c, cx, cy);
        break;

    case NONE:
        return nullret;

    default:
        assert(!"Shouldn't get here!");
    }
    return dupstr(buf);
}

static game_state *execute_move(game_state *state, char *move)
{
    game_state *ret = dup_game(state);
    int x, y, n, flags;
    char c;

    if (!*move) goto badmove;

    while (*move) {
        c = *move;
        if (c == 'S') {
            ret->used_solve = TRUE;
            move++;
        } else if (c == 'L' || c == 'I') {
            move++;
            if (sscanf(move, "%d,%d%n", &x, &y, &n) != 2 ||
                x < 0 || y < 0 || x >= ret->w || y >= ret->h)
                goto badmove;

            flags = GRID(ret, flags, x, y);
            if (flags & F_BLACK) goto badmove;

            /* LIGHT and IMPOSSIBLE are mutually exclusive. */
            if (c == 'L') {
                GRID(ret, flags, x, y) &= ~F_IMPOSSIBLE;
                set_light(ret, x, y, (flags & F_LIGHT) ? 0 : 1);
            } else {
                set_light(ret, x, y, 0);
                GRID(ret, flags, x, y) ^= F_IMPOSSIBLE;
            }
            move += n;
        } else goto badmove;

        if (*move == ';')
            move++;
        else if (*move) goto badmove;
    }
    if (grid_correct(ret)) ret->completed = 1;
    return ret;

badmove:
    free_game(ret);
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

/* XXX entirely cloned from fifteen.c; separate out? */
static void game_compute_size(game_params *params, int tilesize,
			      int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = TILE_SIZE * params->w + 2 * BORDER;
    *y = TILE_SIZE * params->h + 2 * BORDER;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
			  game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
    ds->crad = 3*(tilesize-1)/8;
}

static float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    for (i = 0; i < 3; i++) {
        ret[COL_BLACK * 3 + i] = 0.0F;
        ret[COL_LIGHT * 3 + i] = 1.0F;
        ret[COL_CURSOR * 3 + i] = ret[COL_BACKGROUND * 3 + i] / 2.0F;
        ret[COL_GRID * 3 + i] = ret[COL_BACKGROUND * 3 + i] / 1.5F;

    }

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.25F;
    ret[COL_ERROR * 3 + 2] = 0.25F;

    ret[COL_LIT * 3 + 0] = 1.0F;
    ret[COL_LIT * 3 + 1] = 1.0F;
    ret[COL_LIT * 3 + 2] = 0.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->tilesize = ds->crad = 0;
    ds->w = state->w; ds->h = state->h;

    ds->flags = snewn(ds->w*ds->h, unsigned int);
    for (i = 0; i < ds->w*ds->h; i++)
        ds->flags[i] = -1;

    ds->started = 0;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->flags);
    sfree(ds);
}

/* At some stage we should put these into a real options struct.
 * Note that tile_redraw has no #ifdeffery; it relies on tile_flags not
 * to put those flags in. */
#define HINT_LIGHTS
#define HINT_OVERLAPS
#define HINT_NUMBERS

static unsigned int tile_flags(game_drawstate *ds, game_state *state, game_ui *ui,
                               int x, int y, int flashing)
{
    unsigned int flags = GRID(state, flags, x, y);
    int lights = GRID(state, lights, x, y);
    unsigned int ret = 0;

    if (flashing) ret |= DF_FLASH;
    if (ui && ui->cur_visible && x == ui->cur_x && y == ui->cur_y)
        ret |= DF_CURSOR;

    if (flags & F_BLACK) {
        ret |= DF_BLACK;
        if (flags & F_NUMBERED) {
#ifdef HINT_NUMBERS
            if (number_wrong(state, x, y))
		ret |= DF_NUMBERWRONG;
#endif
            ret |= DF_NUMBERED;
        }
    } else {
#ifdef HINT_LIGHTS
        if (lights > 0) ret |= DF_LIT;
#endif
        if (flags & F_LIGHT) {
            ret |= DF_LIGHT;
#ifdef HINT_OVERLAPS
            if (lights > 1) ret |= DF_OVERLAP;
#endif
        }
        if (flags & F_IMPOSSIBLE) ret |= DF_IMPOSSIBLE;
    }
    return ret;
}

static void tile_redraw(drawing *dr, game_drawstate *ds, game_state *state,
                        int x, int y)
{
    unsigned int ds_flags = GRID(ds, flags, x, y);
    int dx = COORD(x), dy = COORD(y);
    int lit = (ds_flags & DF_FLASH) ? COL_GRID : COL_LIT;

    if (ds_flags & DF_BLACK) {
        draw_rect(dr, dx, dy, TILE_SIZE, TILE_SIZE, COL_BLACK);
        if (ds_flags & DF_NUMBERED) {
            int ccol = (ds_flags & DF_NUMBERWRONG) ? COL_ERROR : COL_LIGHT;
            char str[10];

            /* We know that this won't change over the course of the game
             * so it's OK to ignore this when calculating whether or not
             * to redraw the tile. */
            sprintf(str, "%d", GRID(state, lights, x, y));
            draw_text(dr, dx + TILE_SIZE/2, dy + TILE_SIZE/2,
                      FONT_VARIABLE, TILE_SIZE*3/5,
		      ALIGN_VCENTRE | ALIGN_HCENTRE, ccol, str);
        }
    } else {
        draw_rect(dr, dx, dy, TILE_SIZE, TILE_SIZE,
                  (ds_flags & DF_LIT) ? lit : COL_BACKGROUND);
        draw_rect_outline(dr, dx, dy, TILE_SIZE, TILE_SIZE, COL_GRID);
        if (ds_flags & DF_LIGHT) {
            int lcol = (ds_flags & DF_OVERLAP) ? COL_ERROR : COL_LIGHT;
            draw_circle(dr, dx + TILE_SIZE/2, dy + TILE_SIZE/2, TILE_RADIUS,
                        lcol, COL_BLACK);
        } else if (ds_flags & DF_IMPOSSIBLE) {
            int rlen = TILE_SIZE / 4;
            draw_rect(dr, dx + TILE_SIZE/2 - rlen/2, dy + TILE_SIZE/2 - rlen/2,
                      rlen, rlen, COL_BLACK);
        }
    }

    if (ds_flags & DF_CURSOR) {
        int coff = TILE_SIZE/8;
        draw_rect_outline(dr, dx + coff, dy + coff,
                          TILE_SIZE - coff*2, TILE_SIZE - coff*2, COL_CURSOR);
    }

    draw_update(dr, dx, dy, TILE_SIZE, TILE_SIZE);
}

static void game_redraw(drawing *dr, game_drawstate *ds, game_state *oldstate,
			game_state *state, int dir, game_ui *ui,
			float animtime, float flashtime)
{
    int flashing = FALSE;
    int x,y;

    if (flashtime) flashing = (int)(flashtime * 3 / FLASH_TIME) != 1;

    if (!ds->started) {
        draw_rect(dr, 0, 0,
                  TILE_SIZE * ds->w + 2 * BORDER,
                  TILE_SIZE * ds->h + 2 * BORDER, COL_BACKGROUND);

        draw_rect_outline(dr, COORD(0)-1, COORD(0)-1,
                          TILE_SIZE * ds->w + 2,
                          TILE_SIZE * ds->h + 2,
                          COL_GRID);

        draw_update(dr, 0, 0,
                    TILE_SIZE * ds->w + 2 * BORDER,
                    TILE_SIZE * ds->h + 2 * BORDER);
        ds->started = 1;
    }

    for (x = 0; x < ds->w; x++) {
        for (y = 0; y < ds->h; y++) {
            unsigned int ds_flags = tile_flags(ds, state, ui, x, y, flashing);
            if (ds_flags != GRID(ds, flags, x, y)) {
                GRID(ds, flags, x, y) = ds_flags;
                tile_redraw(dr, ds, state, x, y);
            }
        }
    }
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
			      int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(game_state *oldstate, game_state *newstate,
			       int dir, game_ui *ui)
{
    if (!oldstate->completed && newstate->completed &&
        !oldstate->used_solve && !newstate->used_solve)
        return FLASH_TIME;
    return 0.0F;
}

static int game_wants_statusbar(void)
{
    return FALSE;
}

static int game_timing_state(game_state *state, game_ui *ui)
{
    return TRUE;
}

static void game_print_size(game_params *params, float *x, float *y)
{
    int pw, ph;

    /*
     * I'll use 6mm squares by default.
     */
    game_compute_size(params, 600, &pw, &ph);
    *x = pw / 100.0;
    *y = ph / 100.0;
}

static void game_print(drawing *dr, game_state *state, int tilesize)
{
    int w = state->w, h = state->h;
    int ink = print_mono_colour(dr, 0);
    int paper = print_mono_colour(dr, 1);
    int x, y;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    ads.tilesize = tilesize;
    ds->crad = 3*(tilesize-1)/8;

    /*
     * Border.
     */
    print_line_width(dr, TILE_SIZE / 16);
    draw_rect_outline(dr, COORD(0), COORD(0),
		      TILE_SIZE * w, TILE_SIZE * h, ink);

    /*
     * Grid.
     */
    print_line_width(dr, TILE_SIZE / 24);
    for (x = 1; x < w; x++)
	draw_line(dr, COORD(x), COORD(0), COORD(x), COORD(h), ink);
    for (y = 1; y < h; y++)
	draw_line(dr, COORD(0), COORD(y), COORD(w), COORD(y), ink);

    /*
     * Grid contents.
     */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++) {
            unsigned int ds_flags = tile_flags(ds, state, NULL, x, y, FALSE);
	    int dx = COORD(x), dy = COORD(y);
	    if (ds_flags & DF_BLACK) {
		draw_rect(dr, dx, dy, TILE_SIZE, TILE_SIZE, ink);
		if (ds_flags & DF_NUMBERED) {
		    char str[10];
		    sprintf(str, "%d", GRID(state, lights, x, y));
		    draw_text(dr, dx + TILE_SIZE/2, dy + TILE_SIZE/2,
			      FONT_VARIABLE, TILE_SIZE*3/5,
			      ALIGN_VCENTRE | ALIGN_HCENTRE, paper, str);
		}
	    } else if (ds_flags & DF_LIGHT) {
		draw_circle(dr, dx + TILE_SIZE/2, dy + TILE_SIZE/2,
			    TILE_RADIUS, -1, ink);
	    }
	}
}

#ifdef COMBINED
#define thegame lightup
#endif

const struct game thegame = {
    "Light Up", "games.lightup",
    default_params,
    game_fetch_preset,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    TRUE, game_configure, custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    TRUE, solve_game,
    TRUE, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    game_changed_state,
    interpret_move,
    execute_move,
    PREFERRED_TILE_SIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    TRUE, FALSE, game_print_size, game_print,
    game_wants_statusbar,
    FALSE, game_timing_state,
    0,				       /* mouse_priorities */
};

/* vim: set shiftwidth=4 tabstop=8: */
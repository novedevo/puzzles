/*
 * puzzles.h: header file for my puzzle collection
 */

#ifndef PUZZLES_PUZZLES_H
#define PUZZLES_PUZZLES_H

#include <stdio.h>  /* for FILE */
#include <stdlib.h> /* for size_t */
#include <limits.h> /* for UINT_MAX */
#include <stdint.h>
#include <stdbool.h>

typedef struct random_state random_state;
typedef struct game_params game_params;
typedef struct game_state game_state;
typedef struct game game;

/*
 * Platform routines
 */

#ifdef DEBUG
#define debug(x) (debug_printf x)
void debug_printf(const char *fmt, ...);
#else
#define debug(x)
#endif

/*
 * headers for malloc.c
 */
void *smalloc(size_t size);
void *srealloc(void *p, size_t size);
void sfree(void *p);
char *dupstr(const char *s);
#define snew(type) \
    ((type *)smalloc(sizeof(type)))
#define snewn(number, type) \
    ((type *)smalloc((number) * sizeof(type)))
#define sresize(array, number, type) \
    ((type *)srealloc((array), (number) * sizeof(type)))

/*
 * Data structure containing the function calls and data specific
 * to a particular game.
 */
struct game
{
    game_params *(*default_params)(void);
    bool (*fetch_preset)(int i, char **name, game_params **params);
    void (*decode_params)(game_params *, char const *string);
    char *(*encode_params)(const game_params *, bool full);
    void (*free_params)(game_params *params);
    game_params *(*dup_params)(const game_params *params);
    const char *(*validate_params)(const game_params *params, bool full);
    char *(*new_desc)(const game_params *params, random_state *rs,
                      char **aux, bool interactive);
    const char *(*validate_desc)(const game_params *params, const char *desc);
    game_state *(*dup_game)(const game_state *state);
    void (*free_game)(game_state *state);
    bool can_solve;
    char *(*solve)(const game_state *orig, const game_state *curr,
                   const char *aux, const char **error);
    char *(*text_format)(const game_state *state);
    game_state *(*execute_move)(const game_state *state, const char *move);
    int (*status)(const game_state *state);
};

#endif /* PUZZLES_PUZZLES_H */

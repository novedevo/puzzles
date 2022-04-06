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

typedef struct config_item config_item;
typedef struct random_state random_state;
typedef struct game_params game_params;
typedef struct game_state game_state;
typedef struct game game;

#define ALIGN_VNORMAL 0x000
#define ALIGN_VCENTRE 0x100

#define ALIGN_HLEFT 0x000
#define ALIGN_HCENTRE 0x001
#define ALIGN_HRIGHT 0x002

#define FONT_FIXED 0
#define FONT_VARIABLE 1

/* For printing colours */
#define HATCH_SLASH 1
#define HATCH_BACKSLASH 2
#define HATCH_HORIZ 3
#define HATCH_VERT 4
#define HATCH_PLUS 5
#define HATCH_X 6

/*
 * Structure used to pass configuration data between frontend and
 * game
 */
enum
{
    C_STRING,
    C_CHOICES,
    C_BOOLEAN,
    C_END
};
struct config_item
{
    /* Not dynamically allocated */
    const char *name;
    /* Value from the above C_* enum */
    int type;
    union
    {
        struct
        { /* if type == C_STRING */
            /* Always dynamically allocated and non-NULL */
            char *sval;
        } string;
        struct
        { /* if type == C_CHOICES */
            /*
             * choicenames is non-NULL, not dynamically allocated, and
             * contains a set of option strings separated by a
             * delimiter. The delimiter is also the first character in
             * the string, so for example ":Foo:Bar:Baz" gives three
             * options `Foo', `Bar' and `Baz'.
             */
            const char *choicenames;
            /*
             * Indicates the chosen index from the options in
             * choicenames. In the above example, 0==Foo, 1==Bar and
             * 2==Baz.
             */
            int selected;
        } choices;
        struct
        {
            bool bval;
        } boolean;
    } u;
};

/*
 * Structure used to communicate the presets menu from midend to
 * frontend. In principle, it's also used to pass the same information
 * from game to midend, though games that don't specify a menu
 * hierarchy (i.e. most of them) will use the simpler fetch_preset()
 * function to return an unstructured list.
 *
 * A tree of these structures always belongs to the midend, and only
 * the midend should ever need to free it. The front end should treat
 * them as read-only.
 */
struct preset_menu_entry
{
    char *title;
    /* Exactly one of the next two fields is NULL, depending on
     * whether this entry is a submenu title or an actual preset */
    game_params *params;
    struct preset_menu *submenu;
    /* Every preset menu entry has a number allocated by the mid-end,
     * so that midend_which_preset() can return a value that
     * identifies an entry anywhere in the menu hierarchy. The values
     * will be allocated reasonably densely from 1 upwards (so it's
     * reasonable for the front end to use them as array indices if it
     * needs to store GUI state per menu entry), but no other
     * guarantee is given about their ordering.
     *
     * Entries containing submenus have ids too - not only the actual
     * presets are numbered. */
    int id;
};
struct preset_menu
{
    int n_entries;    /* number of entries actually in use */
    int entries_size; /* space currently allocated in this array */
    struct preset_menu_entry *entries;
};
/* For games which do want to directly return a tree of these, here
 * are convenience routines (in midend.c) for constructing one. These
 * assume that 'title' and 'encoded_params' are already dynamically
 * allocated by the caller; the resulting preset_menu tree takes
 * ownership of them. */
struct preset_menu *preset_menu_new(void);
struct preset_menu *preset_menu_add_submenu(struct preset_menu *parent,
                                            char *title);
void preset_menu_add_preset(struct preset_menu *menu,
                            char *title, game_params *params);
/* Helper routine front ends can use for one of the ways they might
 * want to organise their preset menu usage */
game_params *preset_menu_lookup_by_id(struct preset_menu *menu, int id);

/*
 * Small structure specifying a UI button in a keyboardless front
 * end. The button will have the text of "label" written on it, and
 * pressing it causes the value "button" to be passed to
 * midend_process_key() as if typed at the keyboard.
 *
 * If `label' is NULL (which it likely will be), a generic label can
 * be generated with the button2label() function.
 */
typedef struct key_label
{
    /* What should be displayed to the user by the frontend. Backends
     * can set this field to NULL and have it filled in by the midend
     * with a generic label. Dynamically allocated, but frontends
     * should probably use free_keys() to free instead. */
    char *label;
    int button; /* passed to midend_process_key when button is pressed */
} key_label;

/*
 * Platform routines
 */

/* We can't use #ifdef DEBUG, because Cygwin defines it by default. */
#ifdef DEBUGGING
#define debug(x) (debug_printf x)
void debug_printf(const char *fmt, ...);
#else
#define debug(x)
#endif

/*
 * malloc.c
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
 * dsf.c
 */
int *snew_dsf(int size);

/* Return the canonical element of the equivalence class containing element
 * val.  If 'inverse' is non-NULL, this function will put into it a flag
 * indicating whether the canonical element is inverse to val. */
int dsf_canonify(int *dsf, int val);

/* Allow the caller to specify that two elements should be in the same
 * equivalence class.  If 'inverse' is true, the elements are actually opposite
 * to one another in some sense.  This function will fail an assertion if the
 * caller gives it self-contradictory data, ie if two elements are claimed to
 * be both opposite and non-opposite. */
void dsf_merge(int *dsf, int v1, int v2);
void dsf_init(int *dsf, int len);

/*
 * random.c
 */
random_state *random_new(const char *seed, int len);
random_state *random_copy(random_state *tocopy);
unsigned long random_bits(random_state *state, int bits);
unsigned long random_upto(random_state *state, unsigned long limit);
void random_free(random_state *state);
char *random_state_encode(random_state *state);
random_state *random_state_decode(const char *input);
/* random.c also exports SHA, which occasionally comes in useful. */

typedef struct
{
    uint32_t h[5];
    unsigned char block[64];
    int blkused;
    uint32_t lenhi, lenlo;
} SHA_State;
void SHA_Init(SHA_State *s);
void SHA_Bytes(SHA_State *s, const void *p, int len);
void SHA_Final(SHA_State *s, unsigned char *output);
void SHA_Simple(const void *p, int len, unsigned char *output);

/*
 * findloop.c
 */
struct findloopstate;
struct findloopstate *findloop_new_state(int nvertices);
void findloop_free_state(struct findloopstate *);
/*
 * Callback provided by the client code to enumerate the graph
 * vertices joined directly to a given vertex.
 *
 * Semantics: if vertex >= 0, return one of its neighbours; if vertex
 * < 0, return a previously unmentioned neighbour of whatever vertex
 * was last passed as input. Write to 'ctx' as necessary to store
 * state. In either case, return < 0 if no such vertex can be found.
 */
typedef int (*neighbour_fn_t)(int vertex, void *ctx);
/*
 * Actual function to find loops. 'ctx' will be passed unchanged to
 * the 'neighbour' function to query graph edges. Returns false if no
 * loop was found, or true if one was.
 */
bool findloop_run(struct findloopstate *state, int nvertices,
                  neighbour_fn_t neighbour, void *ctx);
/*
 * Query whether an edge is part of a loop, in the output of
 * find_loops.
 *
 * Due to the internal storage format, if you pass u,v which are not
 * connected at all, the output will be true. (The algorithm actually
 * stores an exhaustive list of *non*-loop edges, because there are
 * fewer of those, so really it's querying whether the edge is on that
 * list.)
 */
bool findloop_is_loop_edge(struct findloopstate *state, int u, int v);

/*
 * Data structure containing the function calls and data specific
 * to a particular game. This is enclosed in a data structure so
 * that a particular platform can choose, if it wishes, to compile
 * all the games into a single combined executable rather than
 * having lots of little ones.
 */
struct game
{
    game_params *(*default_params)(void);
    bool (*fetch_preset)(int i, char **name, game_params **params);
    void (*decode_params)(game_params *, char const *string);
    char *(*encode_params)(const game_params *, bool full);
    void (*free_params)(game_params *params);
    game_params *(*dup_params)(const game_params *params);
    bool can_configure;
    config_item *(*configure)(const game_params *params);
    game_params *(*custom_params)(const config_item *cfg);
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

/* A little bit of help to lazy developers */
#define DEFAULT_STATUSBAR_TEXT "Use status_bar() to fill this in."

#endif /* PUZZLES_PUZZLES_H */

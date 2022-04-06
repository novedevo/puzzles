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
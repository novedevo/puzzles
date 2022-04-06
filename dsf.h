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
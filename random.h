#include <stdint.h>

typedef struct random_state random_state;

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
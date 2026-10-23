#include "internal.h"

/* Direct MSB-first CRC-12: polynomial x^12+x^11+x^3+x^2+x+1,
 * initial remainder zero; neither reflection nor final XOR. */

unsigned jt_crc(uint64_t word, int bits) {
    unsigned r = 0;

    for (int i = bits - 1; i >= 0; i--) {
        unsigned feedback = ((r >> 11) ^ (unsigned) (word >> i)) & 1;
        r = ((r << 1) ^ (feedback ? 0x80f : 0)) & 0xfff;
    }

    return r;
}

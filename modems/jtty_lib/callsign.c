#include "internal.h"
#include <string.h>

int jt_call_pack(const char *s, uint32_t *v) {
    size_t n = strlen(s);
    int    area = -1;

    if (n < 3 || n > 6 || s[0] == 'Q')
        return -1;

    for (size_t i = 0; i < n; i++) {
        if (!jt_letter(s[i]) && !jt_digit(s[i]))
            return -1;
        if (jt_digit(s[i]))
            area = (int) i;
    }

    if ((area != 1 && area != 2) || n - (size_t) area - 1 < 1 || n - (size_t) area - 1 > 3)
        return -1;

    if (!jt_letter(s[0]) && !jt_letter(s[1]))
        return -1;

    char c[7] = "      ";

    memcpy(c + (area == 1), s, n);

    int      a = jt_index(" 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ", c[0]);
    int      b = jt_index(jt_alphabet, c[1]);
    uint32_t x = (uint32_t) (a * 36 + b);

    x = x * 10 + (unsigned) (c[2] - '0');

    for (int i = 3; i < 6; i++)
        x = x * 27 + (unsigned) jt_index(" ABCDEFGHIJKLMNOPQRSTUVWXYZ", c[i]);

    *v = x + 6257896u;
    return 0;
}

int jt_call_unpack(uint32_t v, char s[14]) {
    if (v < 6257896u)
        return -1;

    uint32_t x = v - 6257896u;
    char     c[7] = { 0 };

    for (int i = 5; i >= 3; i--) {
        c[i] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ"[x % 27];
        x /= 27;
    }

    c[2] = (char) ('0' + x % 10);
    x /= 10;
    c[1] = jt_alphabet[x % 36];
    x /= 36;

    if (x >= 37)
        return -1;

    c[0] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[x];

    for (int i = 5; i >= 0 && c[i] == ' '; i--)
        c[i] = 0;

    strcpy(s, c + (c[0] == ' '));
    uint32_t check;

    return jt_call_pack(s, &check) == 0 && check == v ? 0 : -1;
}

#include "internal.h"
#include <stdio.h>
#include <string.h>

static int number_ok(int k, int v) {
    return k >= 0 && k <= 7 && v >= 0 && v <= 131071 &&
           (k != 1 || (v >= 1 && v <= 40)) && (k != 2 || (v >= 1 && v <= 90)) && (k != 6 || v <= 9999);
}

static int base36_pack(const char *s, unsigned *v) {
    size_t   n = strlen(s);
    unsigned x = 0;

    if (n != 2 && n != 3)
        return -1;

    for (size_t i = 0; i < n; i++) {
        int d = jt_index(jt_alphabet, s[i]);
        if (d < 0 || d >= 36)
            return -1;
        x = x * 36 + (unsigned) d;
    }

    if (n == 3 && x < 1296)
        return -1;

    *v = x;
    return 0;
}

static int base36_unpack(unsigned v, int n, char *s) {
    if (v >= (n == 2 ? 1296u : 46656u) || (n == 3 && v < 1296))
        return -1;

    s[n] = 0;

    while (n) {
        s[--n] = jt_alphabet[v % 36];
        v /= 36;
    }
    return 0;
}

static int grid_pack(const char *s) {
    if (strlen(s) != 4 || s[0] < 'A' || s[0] > 'R' || s[1] < 'A' || s[1] > 'R' || !jt_digit(s[2]) || !jt_digit(s[3]))
        return -1;
    return (((s[0] - 'A') * 18 + s[1] - 'A') * 10 + s[2] - '0') * 10 + s[3] - '0';
}

int jtty_atom_pack(const jtty_atom *a, int eom, uint64_t *payload) {
    uint32_t w = 0, b = 0, v = 0;
    unsigned tok;
    int      family = 0;

    if (!a || !payload || !memchr(a->text, 0, sizeof(a->text)) || (eom != 0 && eom != 1))
        return -1;

    switch (a->kind) {
        case JTTY_CALL:
            if (a->subtype < 0 || a->subtype > 5 || jt_call_pack(a->text, &v))
                return -1;
            w = (v << 4) + (a->subtype < 4 ? (unsigned) a->subtype * 4 : (unsigned) (a->subtype - 4) * 4 + 1);
            break;

        case JTTY_NUM:
            if (!number_ok(a->subtype, a->value))
                return -1;
            b = ((unsigned) a->role << 26) | ((unsigned) a->subtype << 22) | ((unsigned) a->value << 5);
            break;

        case JTTY_LOC:
            if (a->subtype < 0 || a->subtype > 4 || base36_pack(a->text, &tok))
                return -1;
            family = 1;
            b = ((unsigned) a->role << 26) | ((unsigned) a->subtype << 22) | ((unsigned) (strlen(a->text) - 2) << 21) | (tok << 5);
            break;

        case JTTY_PAIR:
            family = 2;
            if (a->subtype == 0) {
                if (a->value < 1 || a->value > 40 || base36_pack(a->text, &tok))
                    return -1;
                v = ((unsigned) a->value << 17) | ((unsigned) (strlen(a->text) - 2) << 16) | tok;
            } else if (a->subtype == 1) {
                if (a->value < 1 || a->value > 32 || !jtty_section(a->value2) || strlen(a->text) != 1 || a->text[0] < 'A' || a->text[0] > 'F')
                    return -1;
                v = ((unsigned) a->value << 17) | ((unsigned) (a->text[0] - 'A') << 14) | ((unsigned) a->value2 << 7);
            } else
                return -1;
            b = ((unsigned) a->subtype << 24) | (v << 1);
            break;

        case JTTY_TIME:
            if (a->value < 0 || a->value > 16383 || a->value2 < 0 || a->value2 > 1439)
                return -1;
            family = 3;
            b = ((unsigned) a->role << 26) | ((unsigned) a->value << 12) | ((unsigned) a->value2 << 1);
            break;

        case JTTY_CONTROL:
            if (a->subtype < 0 || a->subtype > 17)
                return -1;
            family = 4;
            b = (unsigned) a->subtype << 16;
            break;

        case JTTY_GRID: {
            int g = grid_pack(a->text);
            if (g < 0)
                return -1;
            family = 4;
            b = (1u << 23) | ((unsigned) a->role << 22) | ((unsigned) g << 7);
            break;
        }

        case JTTY_TEXT5:
            if (strlen(a->text) > 5)
                return -1;
            for (size_t i = 0; i < 5; i++) {
                int d = jt_index(jt_alphabet, i < strlen(a->text) ? a->text[i] : ' ');
                if (d < 0)
                    return -1;
                w = (w << 6) | (unsigned) d;
            }
            w = (w << 2) | 3;
            break;

        default:
            return -1;
    }

    if (a->kind != JTTY_CALL && a->kind != JTTY_TEXT5) {
        if ((a->kind == JTTY_NUM || a->kind == JTTY_LOC || a->kind == JTTY_TIME || a->kind == JTTY_GRID) && (a->role < 0 || a->role > 1))
            return -1;
        w = (b << 5) | ((unsigned) family << 2) | 2;
    }

    *payload = ((uint64_t) w << 2) | (unsigned) eom;
    return 0;
}

int jtty_atom_unpack(uint64_t p, jtty_atom *a, int *eom) {
    if (!a || !eom || p >> 34 || (p & 2) || !(p >> 2))
        return -1;

    memset(a, 0, sizeof(*a));
    *eom = 0;

    uint32_t w = (uint32_t) (p >> 2), top = w >> 2, b = w >> 5, v;
    int      type = w & 3, family = top & 7, n;

    if (type < 2) {
        a->kind = JTTY_CALL;
        a->subtype = (int) ((w >> 2) & 3) + 4 * type;

        if (a->subtype > 5 || jt_call_unpack(w >> 4, a->text))
            return -1;
    } else if (type == 3) {
        a->kind = JTTY_TEXT5;
        for (int i = 4; i >= 0; i--) {
            a->text[i] = jt_alphabet[top & 63];
            top >>= 6;
        }
    } else
        switch (family) {
            case 0:
                a->kind = JTTY_NUM;
                a->role = (int) (b >> 26);
                a->subtype = (b >> 22) & 15;
                a->value = (b >> 5) & 131071;
                if ((b & 31) || !number_ok(a->subtype, a->value))
                    return -1;
                break;

            case 1:
                a->kind = JTTY_LOC;
                a->role = (int) (b >> 26);
                a->subtype = (b >> 22) & 15;
                n = 2 + (int) ((b >> 21) & 1);
                if ((b & 31) || a->subtype > 4 || base36_unpack((b >> 5) & 65535, n, a->text))
                    return -1;
                break;

            case 2:
                a->kind = JTTY_PAIR;
                a->subtype = (int) (b >> 24);
                v = (b >> 1) & 8388607;

                if (b & 1)
                    return -1;
                a->value = (int) (v >> 17);

                if (a->subtype == 0) {
                    if (a->value < 1 || a->value > 40 || base36_unpack(v & 65535, 2 + (int) ((v >> 16) & 1), a->text))
                        return -1;
                } else if (a->subtype == 1) {
                    a->value2 = (v >> 7) & 127;
                    a->text[0] = (char) ('A' + ((v >> 14) & 7));
                    if (a->value < 1 || a->value > 32 || a->text[0] > 'F' || !jtty_section(a->value2) || (v & 127))
                        return -1;
                } else
                    return -1;
                break;

            case 3:
                a->kind = JTTY_TIME;
                a->role = (int) (b >> 26);
                a->value = (b >> 12) & 16383;
                a->value2 = (b >> 1) & 2047;

                if ((b & 1) || a->value2 > 1439)
                    return -1;
                break;

            case 4:
                v = b & 8388607;

                if ((b >> 23) == 0) {
                    a->kind = JTTY_CONTROL;
                    a->subtype = (int) (v >> 16);
                    if ((v & 65535) || a->subtype > 17)
                        return -1;
                } else if ((b >> 23) == 1) {
                    a->kind = JTTY_GRID;
                    a->role = (int) (v >> 22);
                    unsigned g = (v >> 7) & 32767;
                    if ((v & 127) || g >= 32400)
                        return -1;
                    a->text[3] = (char) ('0' + g % 10);
                    g /= 10;
                    a->text[2] = (char) ('0' + g % 10);
                    g /= 10;
                    a->text[1] = (char) ('A' + g % 18);
                    a->text[0] = (char) ('A' + g / 18);
                } else
                    return -1;
                break;

            default:
                return -1;
        }

    *eom = (int) (p & 1);
    return 0;
}

int jtty_atom_render(const jtty_atom *a, char text[81]) {
    uint64_t p;

    if (!text || jtty_atom_pack(a, 0, &p))
        return -1;

    const char *prefix = a->role ? "599 " : "";

    switch (a->kind) {
        case JTTY_CALL: {
            const char *fmt[] = { "CQ %s CQ", "%s", "TU %s CQ", "%s TU", "%s AGN?", "TU NOW %s" };
            snprintf(text, 81, fmt[a->subtype], a->text);
            break;
        }

        case JTTY_NUM: {
            int width[] = { 3, 2, 2, 1, 1, 2, 4, 1 };
            snprintf(text, 81, "%s%0*d", prefix, width[a->subtype], a->value);
            break;
        }

        case JTTY_LOC:
        case JTTY_GRID:
            snprintf(text, 81, "%s%s", prefix, a->text);
            break;

        case JTTY_PAIR:
            if (a->subtype == 0)
                snprintf(text, 81, "599 %02d %s", a->value, a->text);
            else
                snprintf(text, 81, "%d%c %s", a->value, a->text[0], jtty_section(a->value2));
            break;

        case JTTY_TIME:
            snprintf(text, 81, "%s%03d %02d%02d", prefix, a->value, a->value2 / 60, a->value2 % 60);
            break;

        case JTTY_CONTROL:
            strcpy(text, jt_controls[a->subtype]);
            break;

        case JTTY_TEXT5:
            snprintf(text, 81, "%-5s", a->text);
            break;

        default:
            return -1;
    }
    return 0;
}

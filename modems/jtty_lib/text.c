#include "internal.h"
#include <stdio.h>
#include <string.h>

static int decimal(const char *s, int *value) {
    size_t n = strlen(s);
    int    v = 0;

    if (n < 1 || n > 6)
        return -1;

    for (size_t i = 0; i < n; i++) {
        if (!jt_digit(s[i]))
            return -1;
        v = v * 10 + s[i] - '0';
    }

    if (v > 131071)
        return -1;

    *value = v;
    return 0;
}

static int consider_atom(const jtty_atom *a, const char *text, jtty_atom *best, int found) {
    char rendered[81];

    if (jtty_atom_render(a, rendered) || strcmp(text, rendered))
        return found;

    if (!found || a->kind < best->kind || (a->kind == best->kind && (a->subtype < best->subtype || (a->subtype == best->subtype && a->role < best->role)))) {
        *best = *a;
        return 1;
    }

    return found;
}

/* Recognize only context-free forms and the explicitly selected profile. */

static int recognize(const char *s, int profile, jtty_atom *best) {
    jtty_atom a = { 0 };
    int       found = 0;
    size_t    n = strlen(s);

    for (int k = 0; k < 6; k++) {
        const char *pre[] = { "CQ ", "", "TU ", "", "", "TU NOW " };
        const char *suf[] = { " CQ", "", " CQ", " TU", " AGN?", "" };
        size_t      l = strlen(pre[k]), r = strlen(suf[k]);

        if (n > l + r && n - l - r <= 6 && !strncmp(s, pre[k], l) && !strcmp(s + n - r, suf[k])) {
            memset(&a, 0, sizeof(a));
            a.kind = JTTY_CALL;
            a.subtype = k;
            memcpy(a.text, s + l, n - l - r);
            found = consider_atom(&a, s, best, found);
        }
    }

    memset(&a, 0, sizeof(a));
    a.kind = JTTY_CONTROL;

    for (int i = 0; i < 18; i++) {
        a.subtype = i;
        found = consider_atom(&a, s, best, found);
    }

    const char *field = s;
    int         role = 0, value;

    if (!strncmp(s, "599 ", 4)) {
        role = 1;
        field = s + 4;
    }

    if (!decimal(field, &value)) {
        memset(&a, 0, sizeof(a));
        a.kind = JTTY_NUM;
        a.role = role;
        a.value = value;
        a.subtype = 7;
        found = consider_atom(&a, s, best, found);

        if (profile == JTTY_RTTY_ROUNDUP && role) {
            a.subtype = 0;
            found = consider_atom(&a, s, best, found);
        }
    }

    if (strlen(field) <= 3 && role) {
        int has_letter = 0;

        for (size_t i = 0; i < strlen(field); i++)
            has_letter |= jt_letter(field[i]);

        if (has_letter) {
            memset(&a, 0, sizeof(a));
            a.kind = JTTY_LOC;
            a.role = 1;
            a.subtype = 3;
            strcpy(a.text, field);
            found = consider_atom(&a, s, best, found);

            if (profile == JTTY_RTTY_ROUNDUP) {
                a.subtype = 0;
                found = consider_atom(&a, s, best, found);
            }
        }
    }

    if (strlen(field) == 4) {
        memset(&a, 0, sizeof(a));
        a.kind = JTTY_GRID;
        a.role = role;
        strcpy(a.text, field);
        found = consider_atom(&a, s, best, found);
    }

    int  count = 0, used = 0;
    char cl = 0, sec[4] = { 0 };

    if (sscanf(s, "%2d%c %3s%n", &count, &cl, sec, &used) == 3 && (size_t) used == n) {
        memset(&a, 0, sizeof(a));
        a.kind = JTTY_PAIR;
        a.subtype = 1;
        a.value = count;
        a.text[0] = cl;

        for (int i = 1; i <= 86; i++)
            if (!strcmp(sec, jtty_section(i))) {
                a.value2 = i;
                found = consider_atom(&a, s, best, found);
            }
    }

    return found;
}

int jtty_pack(const char *text, int profile, uint64_t frames[16], char normalized[81]) {
    if (!text || !frames || !normalized || profile < 0 || profile > 2)
        return -1;

    char   s[81] = { 0 }, canon[81] = { 0 };
    size_t n = 0;

    for (size_t i = 0; i < 80 && text[i]; i++) {
        unsigned char c = (unsigned char) text[i];

        if (c == '~')
            c = ' ';

        if (c >= 'a' && c <= 'z')
            c = (unsigned char) (c - 'a' + 'A');

        if (c == ' ' && (!n || s[n - 1] == ' '))
            continue;

        s[n++] = jt_index(jt_alphabet, (char) c) < 0 ? '#' : (char) c;
    }

    if (n && s[n - 1] == ' ')
        s[--n] = 0;

    if (!n) {
        normalized[0] = 0;
        return 0;
    }

    if (profile == JTTY_RTTY_ROUNDUP) {
        size_t pos = 0, out = 0;
        int    after_report = 0;

        while (pos < n) {
            size_t end = pos;

            while (end < n && s[end] != ' ')
                end++;

            char tok[81] = { 0 }, num[16];

            memcpy(tok, s + pos, end - pos);

            const char *use = tok;
            int         v;

            if (after_report && !decimal(tok, &v)) {
                snprintf(num, sizeof(num), "%03d", v);
                use = num;
            }

            size_t len = strlen(use);

            if (out + len + (out != 0) > 80)
                return -1;

            if (out)
                canon[out++] = ' ';

            memcpy(canon + out, use, len);
            out += len;
            after_report = (use == tok) && !strcmp(tok, "599");
            pos = end + 1;
        }

        strcpy(s, canon);
        n = strlen(s);
    }

    int      cost[81] = { 0 }, next[81] = { 0 };
    uint64_t word[80];

    cost[n] = 0;

    for (int i = (int) n - 1; i >= 0; i--) {
        jtty_atom a = { 0 };
        a.kind = JTTY_TEXT5;
        size_t take = n - (size_t) i;

        if (take > 5)
            take = 5;

        memcpy(a.text, s + i, take);
        next[i] = i + (int) take;
        cost[i] = 1 + cost[next[i]];
        jtty_atom_pack(&a, 0, &word[i]);

        int structured = 0, best_span = 0;

        if (i && s[i - 1] != ' ')
            continue;

        for (int end = i + 1; end <= (int) n; end++) {
            if (end < (int) n && s[end] != ' ')
                continue;

            char span[81] = { 0 };

            memcpy(span, s + i, (size_t) (end - i));
            jtty_atom candidate;

            if (!recognize(span, profile, &candidate))
                continue;

            int stop = end + (end < (int) n), c = 1 + cost[stop];

            if (c < cost[i] || (c == cost[i] && (!structured || stop - i > best_span))) {
                cost[i] = c;
                next[i] = stop;
                structured = 1;
                best_span = stop - i;
                jtty_atom_pack(&candidate, 0, &word[i]);
            }
        }
    }

    if (cost[0] > 16)
        return -1;

    int k = 0;

    for (int i = 0; i < (int) n; i = next[i])
        frames[k++] = word[i];

    frames[k - 1] |= 1;
    strcpy(normalized, s);

    return k;
}

int jtty_unpack(const uint64_t *frames, size_t count, char text[81]) {
    if (!frames || !text || !count || count > 16)
        return -1;

    size_t n = 0;
    text[0] = 0;

    for (size_t i = 0; i < count; i++) {
        jtty_atom a;
        int       eom;
        char      s[81];

        if (jtty_atom_unpack(frames[i], &a, &eom) || jtty_atom_render(&a, s))
            return -1;

        if (eom && i + 1 < count)
            return -1;

        size_t len = strlen(s);

        if (len > 80 - n)
            len = 80 - n;

        memcpy(text + n, s, len);
        n += len;

        if (a.kind != JTTY_TEXT5 && n < 80)
            text[n++] = ' ';
    }

    while (n && text[n - 1] == ' ')
        n--;

    text[n] = 0;
    return 0;
}

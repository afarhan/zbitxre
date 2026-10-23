#include "internal.h"
#include <string.h>

const char jt_alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!\"#$%,&*()_'=[]{}<>|:;";

const char *const jt_controls[] = {
    "AGN?",
    "CALL?",
    "AGN CALL",
    "NR?",
    "AGN NR",
    "EXCH?",
    "STATE?",
    "SECTION?",
    "ZONE?",
    "GRID?",
    "RPRT?",
    "QSL TU",
    "TU",
    "QRZ?",
    "QSO B4",
    "WAIT",
    "NIL?",
    "OK?"
};

static const char *const sections[] = {
    "AB",
    "AK",
    "AL",
    "AR",
    "AZ",
    "BC",
    "CO",
    "CT",
    "DE",
    "EB",
    "EMA",
    "ENY",
    "EPA",
    "EWA",
    "GA",
    "GH",
    "IA",
    "ID",
    "IL",
    "IN",
    "KS",
    "KY",
    "LA",
    "LAX",
    "NS",
    "MB",
    "MDC",
    "ME",
    "MI",
    "MN",
    "MO",
    "MS",
    "MT",
    "NC",
    "ND",
    "NE",
    "NFL",
    "NH",
    "NL",
    "NLI",
    "NM",
    "NNJ",
    "NNY",
    "TER",
    "NTX",
    "NV",
    "OH",
    "OK",
    "ONE",
    "ONN",
    "ONS",
    "OR",
    "ORG",
    "PAC",
    "PR",
    "QC",
    "RI",
    "SB",
    "SC",
    "SCV",
    "SD",
    "SDG",
    "SF",
    "SFL",
    "SJV",
    "SK",
    "SNJ",
    "STX",
    "SV",
    "TN",
    "UT",
    "VA",
    "VI",
    "VT",
    "WCF",
    "WI",
    "WMA",
    "WNY",
    "WPA",
    "WTX",
    "WV",
    "WWA",
    "WY",
    "DX",
    "PE",
    "NB"
};

const char *jtty_section(int i) {
    return i >= 1 && i <= 86 ? sections[i - 1] : NULL;
}

int jt_letter(char c) {
    return c >= 'A' && c <= 'Z';
}

int jt_digit(char c) {
    return c >= '0' && c <= '9';
}

int jt_index(const char *s, char c) {
    const char *p = c ? strchr(s, c) : NULL;
    return p ? (int) (p - s) : -1;
}

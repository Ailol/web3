#include "compiler.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

static uint64_t parse_num(const char **p) {
    skip_ws(p);
    uint64_t v;
    if ((*p)[0] == '0' && ((*p)[1] == 'x' || (*p)[1] == 'X')) {
        v = strtoull(*p, (char **)p, 16);
    } else {
        v = strtoull(*p, (char **)p, 10);
    }
    return v;
}

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

comp_status_t compile_line(chain_t *c, const char *line) {
    const char *p = line;
    skip_ws(&p);

    if (*p == '#' || *p == '\0' || *p == '\n') return COMP_OK;

    if (starts_with(p, "NOP")) {
        return chain_push(c, EVT_NOP, 0, 0, 0) == CHAIN_OK ? COMP_OK : COMP_ERR_CHAIN;
    }

    if (starts_with(p, "SET")) {
        p += 3;
        uint64_t offset = parse_num(&p);
        uint64_t width  = parse_num(&p);
        uint64_t value  = parse_num(&p);
        return chain_push(c, EVT_SET, offset, value, width) == CHAIN_OK
            ? COMP_OK : COMP_ERR_CHAIN;
    }

    if (starts_with(p, "CLEAR")) {
        p += 5;
        uint64_t offset = parse_num(&p);
        uint64_t width  = parse_num(&p);
        return chain_push(c, EVT_CLEAR, offset, 0, width) == CHAIN_OK
            ? COMP_OK : COMP_ERR_CHAIN;
    }

    if (starts_with(p, "MATCH")) {
        p += 5;
        uint64_t offset = parse_num(&p);
        uint64_t width  = parse_num(&p);
        uint64_t mask   = parse_num(&p);
        uint64_t expect = parse_num(&p);
        return chain_push(c, EVT_MATCH, offset, mask, (width << 32) | (expect & 0xFFFFFFFF))
            == CHAIN_OK ? COMP_OK : COMP_ERR_CHAIN;
    }

    if (starts_with(p, "EMIT")) {
        p += 4;
        uint64_t tag = parse_num(&p);
        return chain_push(c, EVT_EMIT, tag, 0, 0) == CHAIN_OK
            ? COMP_OK : COMP_ERR_CHAIN;
    }

    if (starts_with(p, "BRANCH")) {
        p += 6;
        uint64_t target = parse_num(&p);
        return chain_push(c, EVT_BRANCH, target, 0, 0) == CHAIN_OK
            ? COMP_OK : COMP_ERR_CHAIN;
    }

    if (starts_with(p, "HALT")) {
        return chain_push(c, EVT_HALT, 0, 0, 0) == CHAIN_OK
            ? COMP_OK : COMP_ERR_CHAIN;
    }

    return COMP_ERR_SYNTAX;
}

comp_status_t compile_program(chain_t *c, const char *source) {
    const char *p = source;
    char line[512];

    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len;
        if (eol) {
            len = (size_t)(eol - p);
        } else {
            len = strlen(p);
        }
        if (len >= sizeof(line)) len = sizeof(line) - 1;

        memcpy(line, p, len);
        line[len] = '\0';

        comp_status_t s = compile_line(c, line);
        if (s != COMP_OK) return s;

        p += len;
        if (*p == '\n') p++;
    }
    return COMP_OK;
}

comp_status_t compile_file(chain_t *c, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return COMP_ERR_CHAIN;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        comp_status_t s = compile_line(c, line);
        if (s != COMP_OK) { fclose(f); return s; }
    }
    fclose(f);
    return COMP_OK;
}

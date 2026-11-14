#include "net.h"

/* HTTP/1.0 GET client + HTML -> plain text lines.
 * HTTP/1.0 with Connection: close keeps things simple: no chunked
 * encoding to worry about, end of body = connection close (or Content-Length). */

static int eq_ci(const char* a, const char* b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (!kci_eq(a[i], b[i])) return 0;
    return 1;
}

/* find header value (case-insensitive) in header block; returns 1 + fills val */
static int find_header(const char* hdr, const char* name, char* val, int valcap) {
    uint32_t nlen = (uint32_t)kstrlen(name);
    const char* p = hdr;
    while (*p) {
        const char* eol = p;
        while (*eol && *eol != '\n') eol++;
        if (eol == p) break;                      /* blank line = end of headers */
        if ((uint32_t)(eol - p) > nlen && eq_ci(p, name, nlen) && p[nlen] == ':') {
            const char* v = p + nlen + 1;
            while (v < eol && (*v == ' ' || *v == '\t')) v++;
            int i = 0;
            while (v < eol && *v != '\r' && i < valcap - 1) val[i++] = *v++;
            val[i] = 0;
            return 1;
        }
        p = (*eol == '\r' && eol[1] == '\n') ? eol + 2 : eol + 1;
    }
    return 0;
}

static int parse_url(const char* url, char* host, int hostcap,
                     uint16_t* port, char* path, int pathcap, char* err) {
    const char* p = url;
    if (kci_starts(p, "https://")) { kstrcpy(err, "HTTPS not supported (HTTP only)"); return -1; }
    if (kci_starts(p, "http://")) p += 7;
    /* host */
    int i = 0;
    while (*p && *p != ':' && *p != '/' && *p != '?' && i < hostcap - 1) host[i++] = *p++;
    host[i] = 0;
    if (!i) { kstrcpy(err, "bad url (no host)"); return -1; }
    *port = 80;
    if (*p == ':') {
        p++;
        uint32_t v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (uint32_t)(*p - '0'); p++; }
        if (v > 65535) { kstrcpy(err, "bad port"); return -1; }
        *port = (uint16_t)v;
    }
    /* path */
    int j = 0;
    if (*p == '?') path[j++] = '/';
    if (*p == '/' || *p == '?') {
        while (*p && j < pathcap - 1) path[j++] = *p++;
    }
    if (j == 0) { path[0] = '/'; j = 1; }
    path[j] = 0;
    return 0;
}

/* de-chunk in place; returns new length or 0 on error */
static uint32_t dechunk(uint8_t* b, uint32_t len) {
    uint32_t rd = 0, wr = 0;
    while (rd < len) {
        /* size line */
        uint32_t sz = 0, p = rd;
        while (p < len && b[p] != '\r' && b[p] != '\n' && b[p] != ';') {
            char ch = (char)b[p];
            int d;
            if (ch >= '0' && ch <= '9') d = ch - '0';
            else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
            else return 0;
            sz = sz * 16 + (uint32_t)d;
            p++;
        }
        while (p < len && b[p] != '\n') p++;       /* skip to end of size line */
        if (p >= len) return 0;
        p++;                                       /* past \n */
        if (sz == 0) return wr;
        if (p + sz > len) return 0;
        memcpy(b + wr, b + p, sz);
        wr += sz; p += sz;
        if (p + 1 < len && b[p] == '\r') p++;
        if (p < len && b[p] == '\n') p++;
        rd = p;
    }
    return wr;
}

int http_fetch(const char* url, char* out, uint32_t cap, uint32_t* out_len,
               int* status, char* err) {
    char cur[256];
    if (kstrlen(url) >= sizeof cur) { kstrcpy(err, "url too long"); return -1; }
    kstrcpy(cur, url);
    if (!kstarts(cur, "http://") && !kci_starts(cur, "http://")) {
        if (kci_starts(cur, "https://")) { kstrcpy(err, "HTTPS not supported (HTTP only)"); return -1; }
        /* allow bare host/path */
        char tmp[256];
        kstrcpy(tmp, "http://");
        kstrcat(tmp, cur);
        kstrcpy(cur, tmp);
    }

    for (int hop = 0; hop < 5; hop++) {
        char host[80], path[160], err2[64];
        uint16_t port;
        if (parse_url(cur, host, sizeof host, &port, path, sizeof path, err) < 0) return -1;

        uint32_t ip;
        if (net_resolve(host, &ip, err2) < 0) { kstrcpy(err, err2); return -1; }
        serial_puts("[http] DNS resolved\n");

        if (tcp_connect(ip, port, err2) < 0) { kstrcpy(err, err2); return -1; }
        serial_puts("[http] TCP connected\n");

        /* request */
        char req[400];
        kstrcpy(req, "GET ");
        kstrcat(req, path);
        kstrcat(req, " HTTP/1.0\r\nHost: ");
        kstrcat(req, host);
        if (port != 80) {
            char rev[6], num[6];
            int v = port, r = 0, k = 0;
            do { rev[r++] = (char)('0' + v % 10); v /= 10; } while (v && r < 5);
            while (r) num[k++] = rev[--r];
            num[k] = 0;
            kstrcat(req, ":");
            kstrcat(req, num);
        }
        kstrcat(req, "\r\nUser-Agent: KaelOS/1.0\r\nAccept: */*\r\n");
        kstrcat(req, "Accept-Encoding: identity\r\nConnection: close\r\n\r\n");

        uint32_t rlen = (uint32_t)kstrlen(req);
        if (rlen > 1460) { tcp_close(); kstrcpy(err, "request too long"); return -1; }
        if (tcp_send(req, rlen, err2) < 0) { tcp_close(); kstrcpy(err, err2); return -1; }

        /* read response */
        uint32_t got = 0;
        int done = 0;
        int hdr_seen = 0;
        uint32_t hdr_end = 0, body_start = 0;
        int32_t want = -1;                        /* content-length */
        int chunked = 0;

        while (!done && got < cap) {
            int n = tcp_recv(out + got, cap - got, 10000, err2);
            if (n < 0) {
                if (got == 0) { tcp_close(); kstrcpy(err, err2); return -1; }
                break;                            /* use what we have */
            }
            if (n == 0) break;                    /* FIN */
            got += (uint32_t)n;

            if (!hdr_seen && got >= 4) {
                for (uint32_t i = 0; i + 3 < got; i++) {
                    if (out[i] == '\r' && out[i + 1] == '\n' &&
                        out[i + 2] == '\r' && out[i + 3] == '\n') {
                        hdr_seen = 1;
                        hdr_end = i + 4;
                        break;
                    }
                }
                if (hdr_seen) {
                    char hb[1024];
                    uint32_t hl = hdr_end < 1023 ? hdr_end : 1023;
                    memcpy(hb, out, hl);
                    hb[hl] = 0;
                    char tmp[32];
                    if (find_header(hb, "content-length", tmp, sizeof tmp)) {
                        int32_t v = 0;
                        for (char* q = tmp; *q >= '0' && *q <= '9'; q++)
                            v = v * 10 + (*q - '0');
                        want = v;
                    }
                    if (find_header(hb, "transfer-encoding", tmp, sizeof tmp)) {
                        if (kci_starts(tmp, "chunked")) chunked = 1;
                    }
                    body_start = hdr_end;
                    if (!chunked && want >= 0 &&
                        (int32_t)(got - body_start) >= want) done = 1;
                }
            }
            if (hdr_seen && !chunked && want >= 0 &&
                (int32_t)(got - body_start) >= want) done = 1;
        }
        tcp_close();
        if (!hdr_seen) { kstrcpy(err, "bad response"); return -1; }
        serial_puts("[http] response headers\n");

        /* status line */
        int st = 0;
        {
            const char* p = out;
            if (kstarts(p, "HTTP/")) {
                const char* q = p + 5;
                while (*q && *q != ' ') q++;
                while (*q == ' ') q++;
                while (*q >= '0' && *q <= '9') st = st * 10 + (*q++ - '0');
            }
        }
        *status = st;

        char hdr[hdr_end + 1];
        memcpy(hdr, out, hdr_end);
        hdr[hdr_end] = 0;

        if (st >= 300 && st < 400) {
            char loc[200];
            if (find_header(hdr, "location", loc, sizeof loc)) {
                char next[260];
                if (kci_starts(loc, "http://")) {
                    if (kstrlen(loc) >= sizeof next) { kstrcpy(err, "redirect too long"); return -1; }
                    kstrcpy(next, loc);
                } else if (kci_starts(loc, "https://")) {
                    kstrcpy(err, "redirected to HTTPS (not supported)");
                    return -1;
                } else if (loc[0] == '/') {
                    kstrcpy(next, "http://");
                    kstrcat(next, host);
                    if (port != 80) { /* rare; omit port - fine for default servers */ }
                    kstrcat(next, loc);
                } else {
                    kstrcpy(err, "bad redirect");
                    return -1;
                }
                if (kstrlen(next) >= sizeof cur) { kstrcpy(err, "redirect too long"); return -1; }
                kstrcpy(cur, next);
                continue;                          /* follow */
            }
            kstrcpy(err, "no redirect location");
            return -1;
        }
        if (st != 200) {
            /* build "HTTP 404" style message */
            char num[8];
            int v = st, k = 0;
            char rev[8]; int r = 0;
            if (v == 0) rev[r++] = '0';
            while (v > 0) { rev[r++] = (char)('0' + v % 10); v /= 10; }
            while (r > 0) num[k++] = rev[--r];
            num[k] = 0;
            kstrcpy(err, "HTTP ");
            kstrcat(err, num);
            kstrcat(err, " from server");
            return -1;
        }

        /* body */
        uint32_t blen = got - body_start;
        uint8_t* body = (uint8_t*)out + body_start;
        if (chunked) {
            blen = dechunk(body, blen);
            if (blen == 0 && body_start != got) { kstrcpy(err, "bad chunked encoding"); return -1; }
        } else if (want >= 0 && (int32_t)blen > want) {
            blen = (uint32_t)want;
        }

        /* content-type sanity: only render text-ish */
        char ct[64];
        if (find_header(hdr, "content-type", ct, sizeof ct)) {
            int ok = kci_starts(ct, "text/") || kci_starts(ct, "application/xhtml") ||
                     kci_starts(ct, "application/xml") || kci_starts(ct, "application/json");
            if (!ok) {
                kstrcpy(err, "not a text page (");
                /* trim ct at ';' */
                char* s2 = ct; while (*s2 && *s2 != ';' && kstrlen(err) < 56) { char b2[2] = {*s2, 0}; kstrcat(err, b2); s2++; }
                kstrcat(err, ")");
                return -1;
            }
        }

        /* move body to front */
        if (body_start) memcpy(out, body, blen);
        *out_len = blen;
        return 0;
    }
    kstrcpy(err, "too many redirects");
    return -1;
}

/* ---------------- HTML -> text ---------------- */

static void emit_line(char lines[][76], int max_lines, int* count,
                      char* line, int* pos) {
    /* trim trailing spaces */
    while (*pos > 0 && line[*pos - 1] == ' ') (*pos)--;
    line[*pos] = 0;
    if (*count < max_lines) {
        kstrcpy(lines[*count], line);
        (*count)++;
    }
    *pos = 0;
}

static int entity_val(const char* e, uint32_t len, char* out) {
    /* e points after '&', len chars until ';' (already checked) */
    if (len == 3 && eq_ci(e, "amp", 3)) { out[0] = '&'; return 1; }
    if (len == 2 && eq_ci(e, "lt", 2)) { out[0] = '<'; return 1; }
    if (len == 2 && eq_ci(e, "gt", 2)) { out[0] = '>'; return 1; }
    if (len == 4 && eq_ci(e, "quot", 4)) { out[0] = '"'; return 1; }
    if (len == 4 && eq_ci(e, "apos", 4)) { out[0] = '\''; return 1; }
    if (len == 4 && eq_ci(e, "nbsp", 4)) { out[0] = ' '; return 1; }
    if (len == 4 && eq_ci(e, "copy", 4)) { out[0] = 'c'; out[1] = ')'; return 2; }
    if (len == 3 && eq_ci(e, "reg", 3)) { out[0] = 'R'; out[1] = ')'; return 2; }
    if (e[0] == '#') {
        uint32_t v = 0;
        uint32_t i = 1;
        if (i < len && (e[i] == 'x' || e[i] == 'X')) {
            for (i++; i < len; i++) {
                char ch = e[i]; int d;
                if (ch >= '0' && ch <= '9') d = ch - '0';
                else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
                else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
                else return 0;
                v = v * 16 + (uint32_t)d;
            }
        } else {
            for (; i < len; i++) {
                if (e[i] < '0' || e[i] > '9') return 0;
                v = v * 10 + (uint32_t)(e[i] - '0');
            }
        }
        if (v == 0 || v > 0xFFFF) return 0;
        if (v < 128) { out[0] = (char)v; return 1; }
        /* non-ascii: map to '?' (font is ASCII) */
        out[0] = '?'; return 1;
    }
    return 0;
}

static int is_block_tag(const char* t, uint32_t len) {
    static const char* tags[] = {
        "p", "div", "br", "tr", "li", "h1", "h2", "h3", "h4", "h5", "h6",
        "hr", "ul", "ol", "table", "section", "article", "header", "footer",
        "blockquote", "pre", "figure", "nav", "aside", "center", "form",
        "option", "dt", "dd", "figcaption", "address", "title", 0
    };
    for (int i = 0; tags[i]; i++) {
        if (kstrlen(tags[i]) == len) {
            uint32_t j = 0;
            for (; j < len; j++) if (!kci_eq(t[j], tags[i][j])) break;
            if (j == len) return 1;
        }
    }
    return 0;
}

int html_to_lines(const char* html, char lines[][76], int max_lines, int* count,
                  char* title, int title_cap) {
    *count = 0;
    if (title && title_cap) title[0] = 0;

    const char* p = html;
    char line[76];
    int pos = 0;
    char word[64];
    int wlen = 0;
    int in_title = 0;

    while (*p) {
        if (*p == '<') {
            p++;
            if (p[0] == '!' || p[0] == '?') {      /* comment / doctype / pi */
                while (*p && !(p[0] == '>' )) p++;
                if (*p) p++;
                continue;
            }
            int closing = 0;
            if (*p == '/') { closing = 1; p++; }
            /* tag name */
            const char* tn = p;
            uint32_t tlen = 0;
            while (p[tlen] && ((p[tlen] >= 'a' && p[tlen] <= 'z') ||
                               (p[tlen] >= 'A' && p[tlen] <= 'Z') ||
                               (p[tlen] >= '0' && p[tlen] <= '9'))) tlen++;
            /* skip to '>' (naive: no quote handling) */
            const char* save = p;
            while (*p && *p != '>') p++;
            if (*p) p++;
            (void)save;

            if (tlen == 0) continue;

            /* drop script/style/head-content entirely */
            if (!closing && (eq_ci(tn, "script", 6) || eq_ci(tn, "style", 5))) {
                const char* close = eq_ci(tn, "script", 6) ? "</script" : "</style";
                while (*p) {
                    if (kci_starts(p, close)) {
                        while (*p && *p != '>') p++;
                        if (*p) p++;
                        break;
                    }
                    p++;
                }
                continue;
            }
            if (eq_ci(tn, "title", 5)) {
                if (!closing && title && title_cap) {
                    int ti = 0;
                    while (*p && ti < title_cap - 1) {
                        if (p[0] == '<') break;
                        if (p[0] == '&') {
                            const char* e = p + 1;
                            uint32_t el = 0;
                            while (e[el] && e[el] != ';' && el < 12) el++;
                            if (e[el] == ';') {
                                char tmp[4];
                                int n = entity_val(e, el, tmp);
                                if (n) { for (int k = 0; k < n; k++) title[ti++] = tmp[k]; p = e + el + 1; continue; }
                            }
                        }
                        title[ti++] = *p++;
                    }
                    title[ti] = 0;
                    while (*p && *p != '<') p++;
                }
                continue;
            }

            if (is_block_tag(tn, tlen)) {
                /* flush pending word + line */
                if (wlen) {
                    if (pos + wlen < 75) {
                        for (int i = 0; i < wlen; i++) line[pos++] = word[i];
                    }
                    wlen = 0;
                }
                if (closing || eq_ci(tn, "br", 2) || eq_ci(tn, "hr", 2) ||
                    eq_ci(tn, "li", 2) || eq_ci(tn, "p", 1) || eq_ci(tn, "title", 5)) {
                    emit_line(lines, max_lines, count, line, &pos);
                } else if (eq_ci(tn, "h1", 2) || eq_ci(tn, "h2", 2) || eq_ci(tn, "h3", 2) ||
                           eq_ci(tn, "h4", 2) || eq_ci(tn, "h5", 2) || eq_ci(tn, "h6", 2)) {
                    emit_line(lines, max_lines, count, line, &pos);
                }
                /* list bullets */
                if (!closing && eq_ci(tn, "li", 2)) {
                    if (pos < 73) { line[pos++] = '-'; line[pos++] = ' '; }
                }
                continue;
            }
            /* inline tag: acts as word separator (space will come from text) */
            continue;
        }

        /* text */
        char ch = *p;
        if (ch == '&') {
            const char* e = p + 1;
            uint32_t el = 0;
            while (e[el] && e[el] != ';' && el < 12) el++;
            if (e[el] == ';') {
                char tmp[4];
                int n = entity_val(e, el, tmp);
                if (n) {
                    for (int k = 0; k < n; k++) {
                        if (tmp[k] == ' ' || tmp[k] == '\n' || tmp[k] == '\r') {
                            if (wlen) { if (pos + wlen + 1 < 75) { for (int i = 0; i < wlen; i++) line[pos++] = word[i]; } wlen = 0; }
                        } else if (wlen < 63) word[wlen++] = tmp[k];
                    }
                    p = e + el + 1;
                    continue;
                }
            }
            /* unknown entity: treat '&' as text */
        }
        p++;

        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
        if (ch == ' ') {
            if (wlen) {
                if (pos + wlen + 1 >= 75) emit_line(lines, max_lines, count, line, &pos);
                if (pos + wlen + 1 < 75) {
                    for (int i = 0; i < wlen; i++) line[pos++] = word[i];
                    if (pos < 74 && (pos == 0 || line[pos - 1] != ' ')) line[pos++] = ' ';
                }
                wlen = 0;
            } else {
                if (pos > 0 && pos < 74 && line[pos - 1] != ' ') line[pos++] = ' ';
            }
            continue;
        }
        if ((unsigned char)ch < 32) continue;
        if (wlen < 63) word[wlen++] = ch;
        /* eager wrap if word already too long */
        if (wlen >= 63) {
            if (pos + wlen + 1 >= 75) emit_line(lines, max_lines, count, line, &pos);
            for (int i = 0; i < wlen && pos < 74; i++) line[pos++] = word[i];
            wlen = 0;
        }
    }
    if (wlen) {
        if (pos + wlen + 1 >= 75) emit_line(lines, max_lines, count, line, &pos);
        for (int i = 0; i < wlen && pos < 74; i++) line[pos++] = word[i];
    }
    emit_line(lines, max_lines, count, line, &pos);
    (void)in_title;
    return *count;
}

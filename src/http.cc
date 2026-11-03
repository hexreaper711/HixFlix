/*
 * HixFlix's HTTP(S) client: a socket per request (Connection: close), GnuTLS
 * for https with the app's own CA list, and a small JSON reader. Moved out of
 * osub.cc so AllDebrid, TMDB and the posters use the same code.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "http.h"

#include <ctype.h>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include "lang.h"
#include "platform.h"

namespace {

const char *USER_AGENT = "HixFlix v" VLC_PS5_VERSION;

int connect_to(const std::string &host, int port)
{
    struct addrinfo hints, *res = nullptr;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char ps[8];
    snprintf(ps, sizeof ps, "%d", port);
    if (getaddrinfo(host.c_str(), ps, &hints, &res) != 0 || !res)
        return -1;
    int fd = -1;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0)
            continue;
        struct timeval tv = { 15, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* the body of a chunked reply, joined */
std::string unchunk(const std::string &in)
{
    std::string out;
    size_t at = 0;
    while (at < in.size()) {
        size_t eol = in.find("\r\n", at);
        if (eol == std::string::npos)
            break;
        unsigned long n = strtoul(in.c_str() + at, nullptr, 16);
        if (n == 0)
            break;
        at = eol + 2;
        if (at + n > in.size())
            n = in.size() - at;
        out.append(in, at, n);
        at += n + 2;
    }
    return out;
}

/* The bytes of one connection: TLS or plain. */
struct Wire {
    int fd = -1;
    gnutls_session_t tls = nullptr;

    bool send_all(const std::string &data)
    {
        size_t done = 0;
        while (done < data.size()) {
            ssize_t n = tls ? gnutls_record_send(tls, data.data() + done, data.size() - done)
                            : ::send(fd, data.data() + done, data.size() - done, 0);
            if (tls && (n == GNUTLS_E_INTERRUPTED || n == GNUTLS_E_AGAIN))
                continue;
            if (n <= 0)
                return false;
            done += (size_t)n;
        }
        return true;
    }
    ssize_t recv_some(char *buf, size_t size)
    {
        for (;;) {
            ssize_t n = tls ? gnutls_record_recv(tls, buf, size) : ::recv(fd, buf, size, 0);
            if (tls && (n == GNUTLS_E_INTERRUPTED || n == GNUTLS_E_AGAIN))
                continue;
            return n;
        }
    }
};

/* What came back, read to the end of the connection, as a reply. */
void read_reply(Wire &w, const std::string &host, HttpReply &r)
{
    std::string all;
    char buf[16384];
    for (;;) {
        ssize_t n = w.recv_some(buf, sizeof buf);
        if (n <= 0)   /* the end (or a server that just closes) */
            break;
        all.append(buf, (size_t)n);
        if (all.size() > (32u << 20))
            break;
    }
    size_t he = all.find("\r\n\r\n");
    if (he == std::string::npos) {
        r.error = trf("No answer from %s", host.c_str());
        return;
    }
    std::string head = all.substr(0, he + 2);
    r.body = all.substr(he + 4);
    sscanf(head.c_str(), "HTTP/%*s %d", &r.status);
    bool chunked = false;
    size_t at = head.find("\r\n");
    while (at != std::string::npos && at + 2 < head.size()) {
        size_t eol = head.find("\r\n", at + 2);
        std::string line = head.substr(at + 2, eol - at - 2);
        at = eol;
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string name = line.substr(0, colon), value = line.substr(colon + 1);
        while (!value.empty() && value[0] == ' ')
            value.erase(0, 1);
        if (!strcasecmp(name.c_str(), "transfer-encoding") && value.find("chunked") != std::string::npos)
            chunked = true;
        else if (!strcasecmp(name.c_str(), "location"))
            r.location = value;
    }
    if (chunked)
        r.body = unchunk(r.body);
}

std::string request_text(const std::string &method, const std::string &host, const std::string &path,
                         const std::string &headers, const std::string &body)
{
    std::string req = method + " " + path + " HTTP/1.1\r\nHost: " + host +
                      "\r\nUser-Agent: " + USER_AGENT +
                      "\r\nAccept: */*\r\nConnection: close\r\n" + headers;
    if (!body.empty() || method == "POST")
        req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    return req + "\r\n" + body;
}

/* http://host[:port]/path, no TLS (custom poster addresses) */
HttpReply plain(const std::string &method, const std::string &host_port, const std::string &path,
                const std::string &headers, const std::string &body)
{
    HttpReply r;
    std::string host = host_port;
    int port = 80;
    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        port = atoi(host.c_str() + colon + 1);
        host = host.substr(0, colon);
    }
    Wire w;
    w.fd = connect_to(host, port);
    if (w.fd < 0) {
        r.error = trf("Couldn't reach %s", host.c_str());
        return r;
    }
    if (!w.send_all(request_text(method, host_port, path, headers, body)))
        r.error = trf("The connection to %s broke", host.c_str());
    else
        read_reply(w, host, r);
    close(w.fd);
    return r;
}

}  // namespace

HttpReply https(const std::string &method, const std::string &host, const std::string &path,
                const std::string &headers, const std::string &body)
{
    HttpReply r;
    static std::once_flag once;
    std::call_once(once, [] { gnutls_global_init(); });

    Wire w;
    w.fd = connect_to(host, 443);
    if (w.fd < 0) {
        r.error = trf("Couldn't reach %s", host.c_str());
        return r;
    }
    gnutls_certificate_credentials_t cred;
    gnutls_certificate_allocate_credentials(&cred);
    std::string ca = std::string(plat_data_dir()) + "/certs/cacert.pem";
    if (gnutls_certificate_set_x509_trust_file(cred, ca.c_str(), GNUTLS_X509_FMT_PEM) <= 0)
        fprintf(stderr, "http: no certificates in %s\n", ca.c_str());

    gnutls_init(&w.tls, GNUTLS_CLIENT);
    gnutls_set_default_priority(w.tls);
    gnutls_credentials_set(w.tls, GNUTLS_CRD_CERTIFICATE, cred);
    gnutls_server_name_set(w.tls, GNUTLS_NAME_DNS, host.data(), host.size());
    gnutls_session_set_verify_cert(w.tls, host.c_str(), 0);
    gnutls_transport_set_int(w.tls, w.fd);
    gnutls_handshake_set_timeout(w.tls, 15000);

    int ret;
    do
        ret = gnutls_handshake(w.tls);
    while (ret < 0 && !gnutls_error_is_fatal(ret));
    if (ret < 0) {
        fprintf(stderr, "http: TLS with %s: %s\n", host.c_str(), gnutls_strerror(ret));
        r.error = trf("Couldn't make a secure connection to %s", host.c_str());
    } else {
        if (!w.send_all(request_text(method, host, path, headers, body)))
            r.error = trf("The connection to %s broke", host.c_str());
        else
            read_reply(w, host, r);
        gnutls_bye(w.tls, GNUTLS_SHUT_WR);
    }
    gnutls_deinit(w.tls);
    gnutls_certificate_free_credentials(cred);
    close(w.fd);
    return r;
}

bool split_url(const std::string &url, std::string &host, std::string &path)
{
    if (url.compare(0, 8, "https://") != 0)
        return false;
    size_t slash = url.find('/', 8);
    host = url.substr(8, slash == std::string::npos ? std::string::npos : slash - 8);
    path = slash == std::string::npos ? "/" : url.substr(slash);
    return !host.empty();
}

HttpReply https_get(std::string url)
{
    return http_request("GET", url);
}

HttpReply http_request(const std::string &method, std::string url, const std::string &headers,
                       const std::string &body)
{
    HttpReply r;
    for (int hop = 0; hop < 4; hop++) {
        bool tls = url.compare(0, 8, "https://") == 0;
        if (!tls && url.compare(0, 7, "http://") != 0) {
            r.error = trf("Unexpected address %s", url.c_str());
            return r;
        }
        size_t start = tls ? 8 : 7, slash = url.find('/', start);
        std::string host = url.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        std::string path = slash == std::string::npos ? "/" : url.substr(slash);
        if (host.empty()) {
            r.error = trf("Unexpected address %s", url.c_str());
            return r;
        }
        r = tls ? https(method, host, path, headers, body) : plain(method, host, path, headers, body);
        if (method == "GET" && r.error.empty() && r.status >= 300 && r.status < 400 && !r.location.empty()) {
            url = r.location[0] == '/' ? (tls ? "https://" : "http://") + host + r.location : r.location;
            continue;
        }
        return r;
    }
    return r;
}

/* ---- JSON ---- */

std::string Json::text(const char *fallback) const
{
    if (kind == STR)
        return str;
    if (kind == NUM) {
        char b[32];
        if (num == (double)(long long)num)
            snprintf(b, sizeof b, "%lld", (long long)num);
        else
            snprintf(b, sizeof b, "%g", num);
        return b;
    }
    return fallback;
}

double Json::number(double fallback) const
{
    if (kind == NUM)
        return num;
    if (kind == STR && !str.empty()) {
        char *end;
        double v = strtod(str.c_str(), &end);
        return *end ? fallback : v;
    }
    return fallback;
}

namespace {

struct JsonReader {
    const char *p, *end;
    int depth = 0;

    void space()
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            p++;
    }
    static void put_utf8(std::string &o, unsigned c)
    {
        if (c < 0x80) {
            o += (char)c;
        } else if (c < 0x800) {
            o += (char)(0xC0 | c >> 6);
            o += (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            o += (char)(0xE0 | c >> 12);
            o += (char)(0x80 | (c >> 6 & 0x3F));
            o += (char)(0x80 | (c & 0x3F));
        } else {
            o += (char)(0xF0 | c >> 18);
            o += (char)(0x80 | (c >> 12 & 0x3F));
            o += (char)(0x80 | (c >> 6 & 0x3F));
            o += (char)(0x80 | (c & 0x3F));
        }
    }
    unsigned hex4()
    {
        unsigned v = 0;
        for (int i = 0; i < 4 && p < end; i++, p++) {
            char c = *p;
            v = v * 16 + (unsigned)(c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
        }
        return v;
    }
    bool string(std::string &o)
    {
        if (p >= end || *p != '"')
            return false;
        p++;
        while (p < end && *p != '"') {
            if (*p != '\\') {
                o += *p++;
                continue;
            }
            if (++p >= end)
                return false;
            char e = *p++;
            switch (e) {
            case 'n': o += '\n'; break;
            case 't': o += '\t'; break;
            case 'r': o += '\r'; break;
            case 'b': o += '\b'; break;
            case 'f': o += '\f'; break;
            case 'u': {
                unsigned c = hex4();
                if (c >= 0xD800 && c < 0xDC00 && p + 6 <= end && p[0] == '\\' && p[1] == 'u') {
                    p += 2;
                    unsigned lo = hex4();
                    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                }
                put_utf8(o, c);
                break;
            }
            default: o += e;
            }
        }
        if (p >= end)
            return false;
        p++;
        return true;
    }
    bool value(Json &v)
    {
        space();
        if (p >= end || ++depth > 64)
            return false;
        bool ok = true;
        if (*p == '{') {
            v.kind = Json::OBJ;
            p++;
            space();
            if (p < end && *p == '}') {
                p++;
            } else {
                for (;;) {
                    space();
                    std::string k;
                    if (!string(k))
                        return false;
                    space();
                    if (p >= end || *p++ != ':')
                        return false;
                    v.keys.push_back(k);
                    v.items.emplace_back();
                    if (!value(v.items.back()))
                        return false;
                    space();
                    if (p < end && *p == ',') {
                        p++;
                        continue;
                    }
                    if (p < end && *p == '}') {
                        p++;
                        break;
                    }
                    return false;
                }
            }
        } else if (*p == '[') {
            v.kind = Json::ARR;
            p++;
            space();
            if (p < end && *p == ']') {
                p++;
            } else {
                for (;;) {
                    v.items.emplace_back();
                    if (!value(v.items.back()))
                        return false;
                    space();
                    if (p < end && *p == ',') {
                        p++;
                        continue;
                    }
                    if (p < end && *p == ']') {
                        p++;
                        break;
                    }
                    return false;
                }
            }
        } else if (*p == '"') {
            v.kind = Json::STR;
            ok = string(v.str);
        } else if (end - p >= 4 && !strncmp(p, "true", 4)) {
            v.kind = Json::BOOL;
            v.yes = true;
            p += 4;
        } else if (end - p >= 5 && !strncmp(p, "false", 5)) {
            v.kind = Json::BOOL;
            p += 5;
        } else if (end - p >= 4 && !strncmp(p, "null", 4)) {
            p += 4;
        } else {
            std::string num;
            while (p < end && strchr("+-0123456789.eE", *p))
                num += *p++;
            v.kind = Json::NUM;
            v.num = strtod(num.c_str(), nullptr);
            ok = !num.empty();
        }
        depth--;
        return ok;
    }
};

}  // namespace

bool parse_json(const std::string &text, Json &out)
{
    JsonReader r{ text.data(), text.data() + text.size() };
    return r.value(out);
}

std::string json_escape(const std::string &s)
{
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            o += '\\';
            o += (char)c;
        } else if (c < 0x20) {
            char b[8];
            snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else {
            o += (char)c;
        }
    }
    return o;
}

std::string url_encode(const std::string &s)
{
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            o += (char)c;
        } else {
            char b[4];
            snprintf(b, sizeof b, "%%%02X", c);
            o += b;
        }
    }
    return o;
}

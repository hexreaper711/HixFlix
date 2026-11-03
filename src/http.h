/*
 * HixFlix's small HTTP(S) client and JSON reader: our own sockets and GnuTLS
 * (which VLC's build already carries; the certificates are the app's own
 * cacert.pem). Blocking: for background threads only. OpenSubtitles, AllDebrid,
 * TMDB and the poster downloads all go through it.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <string>
#include <vector>

struct HttpReply {
    int status = 0;
    std::string location;
    std::string body;
    std::string error;   /* couldn't talk to the server at all */
};

/* One request to https://host/path (no redirects followed). headers: extra
 * "Name: value\r\n" lines. */
HttpReply https(const std::string &method, const std::string &host, const std::string &path,
                const std::string &headers, const std::string &body);
/* A GET of https://... that follows redirects. */
HttpReply https_get(std::string url);
/* Any http:// or https:// address, redirects followed for GET. */
HttpReply http_request(const std::string &method, std::string url, const std::string &headers = "",
                       const std::string &body = "");
/* splits https://host/path */
bool split_url(const std::string &url, std::string &host, std::string &path);

struct Json {
    enum Kind { NUL, BOOL, NUM, STR, ARR, OBJ } kind = NUL;
    double num = 0;
    bool yes = false;
    std::string str;
    std::vector<Json> items;
    std::vector<std::string> keys;   /* an object: keys[i] names items[i] */

    const Json &operator[](const char *key) const
    {
        static const Json none;
        for (size_t i = 0; i < keys.size(); i++)
            if (keys[i] == key)
                return items[i];
        return none;
    }
    /* what it holds as a string or number, else the fallback */
    std::string text(const char *fallback = "") const;
    double number(double fallback = 0) const;
};

bool parse_json(const std::string &text, Json &out);
std::string json_escape(const std::string &s);
/* "a b&c" -> "a%20b%26c": a form or query value, case kept */
std::string url_encode(const std::string &s);

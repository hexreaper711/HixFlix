/*
 * VLC-PS5's subtitle downloads from OpenSubtitles: its REST API (v1) over the
 * app's small HTTPS client and JSON reader (http.cc).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "osub.h"

#include <algorithm>
#include <ctype.h>
#include <mutex>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>

#include "http.h"
#include "lang.h"
#include "platform.h"
#include "prefs.h"

namespace {

const char *API_HOST = "api.opensubtitles.com";

/* ---- what the interface sees ---- */

std::mutex lock;
OsubState state = OSUB_IDLE;
std::vector<SubResult> results;
std::string message, saved_path;
/* a signed-in account: its token and the server it was given */
std::string token, token_user, base_host;

void finish(OsubState s, const std::string &msg)
{
    std::lock_guard<std::mutex> g(lock);
    state = s;
    message = msg;
}

using Reply = HttpReply;

std::string url_escape(const std::string &s)
{
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            o += (char)tolower(c);
        } else if (c == ' ') {
            o += '+';
        } else {
            char b[4];
            snprintf(b, sizeof b, "%%%02X", c);
            o += b;
        }
    }
    return o;
}

/* what the server said went wrong */
std::string server_error(const Reply &r, const char *what)
{
    if (!r.error.empty())
        return r.error;
    if (r.status == 401 || r.status == 403)
        return "OpenSubtitles refused the API key";
    Json j;
    if (parse_json(r.body, j)) {
        if (j["message"].kind == Json::STR)
            return j["message"].str;
        const Json &errs = j["errors"];
        if (errs.kind == Json::ARR && !errs.items.empty() && errs.items[0].kind == Json::STR)
            return errs.items[0].str;
    }
    if (r.status == 429)
        return tr("OpenSubtitles is busy: try again in a moment");
    return trf("%s failed (%d)", tr(what), r.status);
}

/* ---- the API ---- */

/* The OpenSubtitles hash: the file's size plus the 64-bit words of its first
 * and last 64 KiB. Empty when it can't be read. */
std::string movie_hash(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return "";
    fseek(f, 0, SEEK_END);
    long long size = ftello(f);
    uint64_t hash = (uint64_t)size;
    const size_t chunk = 65536;
    bool ok = size >= (long long)chunk;
    std::vector<unsigned char> buf(chunk);
    for (int part = 0; ok && part < 2; part++) {
        fseeko(f, part ? size - (long long)chunk : 0, SEEK_SET);
        if (fread(buf.data(), 1, chunk, f) != chunk) {
            ok = false;
            break;
        }
        for (size_t i = 0; i < chunk; i += 8) {
            uint64_t w = 0;
            for (int b = 7; b >= 0; b--)
                w = w << 8 | buf[i + b];
            hash += w;
        }
    }
    fclose(f);
    if (!ok)
        return "";
    char out[20];
    snprintf(out, sizeof out, "%016llx", (unsigned long long)hash);
    return out;
}

struct Account {
    std::string key, user, pass;
};

Account account()
{
    return { pref_str("osub_key", ""), pref_str("osub_user", ""), pref_str("osub_pass", "") };
}

std::string api_headers(const Account &a, const std::string &bearer)
{
    std::string h = "Api-Key: " + a.key + "\r\nContent-Type: application/json\r\n";
    if (!bearer.empty())
        h += "Authorization: Bearer " + bearer + "\r\n";
    return h;
}

/* Signs in (once per account); false with msg when it can't. */
bool sign_in(const Account &a, std::string &msg, std::string &bearer, std::string &host)
{
    {
        std::lock_guard<std::mutex> g(lock);
        if (!token.empty() && token_user == a.user) {
            bearer = token;
            host = base_host;
            return true;
        }
    }
    std::string body = "{\"username\":\"" + json_escape(a.user) + "\",\"password\":\"" + json_escape(a.pass) + "\"}";
    Reply r = https("POST", API_HOST, "/api/v1/login", api_headers(a, ""), body);
    Json j;
    if (!r.error.empty() || r.status != 200 || !parse_json(r.body, j) || j["token"].kind != Json::STR) {
        msg = r.status == 401 ? "OpenSubtitles: wrong user name or password" : server_error(r, "Signing in");
        return false;
    }
    bearer = j["token"].str;
    host = j["base_url"].kind == Json::STR && !j["base_url"].str.empty() ? j["base_url"].str : API_HOST;
    if (host.compare(0, 8, "https://") == 0)
        host = host.substr(8);
    while (!host.empty() && host.back() == '/')
        host.pop_back();
    const Json &u = j["user"];
    if (u["allowed_downloads"].kind == Json::NUM)
        msg = trf("Signed in: %d downloads a day", (int)u["allowed_downloads"].num);
    else
        msg = "Signed in";
    std::lock_guard<std::mutex> g(lock);
    token = bearer;
    token_user = a.user;
    base_host = host;
    return true;
}

bool start()
{
    std::lock_guard<std::mutex> g(lock);
    if (state == OSUB_BUSY)
        return false;
    state = OSUB_BUSY;
    message.clear();
    return true;
}

}  // namespace

/* A plain HTTPS download (redirects followed), for other parts of the app. */
bool web_https_get(const std::string &url, std::string &body, std::string &error)
{
    Reply r = https_get(url);
    if (!r.error.empty()) {
        error = r.error;
        return false;
    }
    if (r.status != 200) {
        error = "HTTP " + std::to_string(r.status);
        return false;
    }
    body.swap(r.body);
    return true;
}

bool osub_has_key()
{
    return !pref_str("osub_key", "").empty();
}

void osub_search(const std::string &path, const std::string &name, const std::string &languages)
{
    if (!start())
        return;
    {
        std::lock_guard<std::mutex> g(lock);
        results.clear();
    }
    Account a = account();
    std::thread([a, path, name, languages] {
        std::string hash = path.find("://") == std::string::npos ? movie_hash(path) : "";
        /* the server wants its parameters in order, lower case */
        std::string q = "/api/v1/subtitles?";
        if (!languages.empty())
            q += "languages=" + url_escape(languages) + "&";
        if (!hash.empty())
            q += "moviehash=" + hash + "&";
        q += "query=" + url_escape(name);
        Reply r = https("GET", API_HOST, q, api_headers(a, ""), "");
        if (r.error.empty() && r.status >= 300 && r.status < 400 && !r.location.empty()) {
            std::string host, p;
            if (r.location[0] == '/')
                r = https("GET", API_HOST, r.location, api_headers(a, ""), "");
            else if (split_url(r.location, host, p))
                r = https("GET", host, p, api_headers(a, ""), "");
        }
        Json j;
        if (!r.error.empty() || r.status != 200 || !parse_json(r.body, j)) {
            finish(OSUB_FAILED, server_error(r, "The search"));
            return;
        }
        std::vector<SubResult> found;
        for (const Json &d : j["data"].items) {
            const Json &at = d["attributes"];
            const Json &files = at["files"];
            if (files.items.empty() || files.items[0]["file_id"].kind != Json::NUM)
                continue;
            SubResult s;
            s.file_id = (long long)files.items[0]["file_id"].num;
            s.language = at["language"].str;
            s.release = at["release"].str;
            if (s.release.empty())
                s.release = files.items[0]["file_name"].str;
            s.downloads = (int)at["download_count"].num;
            s.hash_match = at["moviehash_match"].yes;
            found.push_back(s);
        }
        /* exact matches first, then the most downloaded */
        std::stable_sort(found.begin(), found.end(), [](const SubResult &x, const SubResult &y) {
            if (x.hash_match != y.hash_match)
                return x.hash_match;
            return x.downloads > y.downloads;
        });
        {
            std::lock_guard<std::mutex> g(lock);
            results = found;
        }
        finish(OSUB_DONE, found.empty() ? "No subtitles found" : "");
    }).detach();
}

void osub_download(const SubResult &res, const std::string &save_base, const std::string &fallback_base)
{
    if (!start())
        return;
    Account a = account();
    std::thread([a, res, save_base, fallback_base] {
        std::string msg, bearer, host = API_HOST;
        if (!a.user.empty() && !sign_in(a, msg, bearer, host)) {
            finish(OSUB_FAILED, msg);
            return;
        }
        std::string body = "{\"file_id\":" + std::to_string(res.file_id) + "}";
        Reply r = https("POST", host, "/api/v1/download", api_headers(a, bearer), body);
        Json j;
        if (!r.error.empty() || r.status != 200 || !parse_json(r.body, j) || j["link"].kind != Json::STR) {
            finish(OSUB_FAILED, r.status == 406 || r.status == 429 ? server_error(r, "The download") +
                                                         (a.user.empty() ? std::string(" ") + tr("(an account gives more a day)") : "")
                                                       : server_error(r, "The download"));
            return;
        }
        std::string left = j["remaining"].kind == Json::NUM
                               ? trf("%d downloads left today", (int)j["remaining"].num) : "";
        Reply f = https_get(j["link"].str);
        if (!f.error.empty() || f.status != 200 || f.body.empty()) {
            finish(OSUB_FAILED, f.error.empty() ? "Couldn't fetch the subtitle file" : f.error);
            return;
        }
        std::string lang = res.language.empty() ? "sub" : res.language;
        std::string out;
        for (const std::string &base : { save_base, fallback_base }) {
            if (base.empty())
                continue;
            std::string p = base + "." + lang + ".srt";
            FILE *o = fopen(p.c_str(), "wb");
            if (!o)
                continue;
            bool ok = fwrite(f.body.data(), 1, f.body.size(), o) == f.body.size();
            ok = fclose(o) == 0 && ok;
            if (ok) {
                out = p;
                break;
            }
            remove(p.c_str());
        }
        if (out.empty()) {
            finish(OSUB_FAILED, "Couldn't save the subtitle file");
            return;
        }
        {
            std::lock_guard<std::mutex> g(lock);
            saved_path = out;
        }
        finish(OSUB_DONE, left);
    }).detach();
}

void osub_sign_in()
{
    if (!start())
        return;
    Account a = account();
    {
        std::lock_guard<std::mutex> g(lock);
        token.clear();
    }
    std::thread([a] {
        if (a.key.empty()) {
            finish(OSUB_FAILED, "Enter your API key first");
            return;
        }
        std::string msg, bearer, host;
        if (a.user.empty()) {
            /* no account: try the key alone. Searches and the informational
             * calls answer any key, even none; "discover" refuses a bad one
             * (403, "You cannot consume this service"). */
            Reply r = https("GET", API_HOST, "/api/v1/discover/popular?languages=en", api_headers(a, ""), "");
            bool ok = r.error.empty() && r.status == 200;
            finish(ok ? OSUB_DONE : OSUB_FAILED,
                   ok ? "The API key works"
                      : r.error.empty() && (r.status == 401 || r.status == 403) ? "OpenSubtitles refused this API key"
                                                                               : server_error(r, "Checking the key"));
            return;
        }
        bool ok = sign_in(a, msg, bearer, host);
        finish(ok ? OSUB_DONE : OSUB_FAILED, msg);
    }).detach();
}

OsubState osub_state()
{
    std::lock_guard<std::mutex> g(lock);
    OsubState s = state;
    /* a finished job is reported once */
    if (s == OSUB_DONE || s == OSUB_FAILED)
        state = OSUB_IDLE;
    return s;
}

std::vector<SubResult> osub_results()
{
    std::lock_guard<std::mutex> g(lock);
    return results;
}

std::string osub_message()
{
    std::lock_guard<std::mutex> g(lock);
    return message;
}

std::string osub_saved_path()
{
    std::lock_guard<std::mutex> g(lock);
    std::string p = saved_path;
    saved_path.clear();
    return p;
}

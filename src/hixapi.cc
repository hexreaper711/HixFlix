/*
 * HixFlix's web services: AllDebrid and TMDB over http.cc. Ported from HIX's
 * supabase/functions/_shared/alldebrid.ts and tmdb.ts, and lib/alldebrid.ts
 * (the unlock that runs on the device).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hixapi.h"

#include <stdio.h>
#include <unistd.h>

#include "hixdb.h"

#ifndef HIXFLIX_TMDB_KEY
#define HIXFLIX_TMDB_KEY ""
#endif

namespace {

const char *AD_HOST = "api.alldebrid.com";
const char *AGENT = "HixFlix";

/* AllDebrid's answer: its data, or what went wrong in its words. */
bool ad_call(const std::string &key, const std::string &path, const std::string &form, Json *data,
             std::string *error)
{
    std::string headers = "Authorization: Bearer " + key + "\r\nAccept: application/json\r\n"
                          "Content-Type: application/x-www-form-urlencoded\r\n";
    HttpReply r = https("POST", AD_HOST, path, headers, "agent=" + std::string(AGENT) + form);
    if (!r.error.empty()) {
        *error = r.error;
        return false;
    }
    Json j;
    if (!parse_json(r.body, j)) {
        *error = "AllDebrid returned non-JSON (HTTP " + std::to_string(r.status) + ")";
        return false;
    }
    const Json &e = j["error"];
    if (e.kind == Json::OBJ) {
        std::string code = e["code"].text(), msg = e["message"].text();
        if (code == "AUTH_BAD_APIKEY" || code == "AUTH_MISSING_APIKEY")
            *error = "AllDebrid refused the API key";
        else if (code == "AUTH_BLOCKED")
            *error = "AllDebrid blocked this key: confirm the new device in your email";
        else
            *error = "AllDebrid: " + (msg.empty() ? code : msg);
        return false;
    }
    if (r.status != 200) {
        *error = "AllDebrid HTTP " + std::to_string(r.status);
        return false;
    }
    *data = j["data"];
    return true;
}

void flatten(const Json &node, const std::string &magnet, std::vector<AdFile> *out)
{
    if (node["s"].kind == Json::NUM && node["l"].kind == Json::STR)
        out->push_back({ magnet, node["n"].text(), node["l"].str, (int64_t)node["s"].num });
    for (const Json &child : node["e"].items)
        flatten(child, magnet, out);
}

/* TMDB: a GET, retried on 429 and 5xx (a hiccup shouldn't fail a whole match). */
bool tmdb_get(const std::string &key, const std::string &path, const std::string &query, Json *out,
              std::string *error)
{
    bool bearer = key.size() > 64; /* a v4 read access token (a JWT), not a v3 key */
    std::string url = "https://api.themoviedb.org/3" + path + "?" + query;
    std::string headers = "Accept: application/json\r\n";
    if (bearer)
        headers += "Authorization: Bearer " + key + "\r\n";
    else
        url += (query.empty() ? "" : "&") + std::string("api_key=") + url_encode(key);
    for (int attempt = 0; attempt < 3; attempt++) {
        HttpReply r = http_request("GET", url, headers);
        if (r.error.empty() && r.status == 200 && parse_json(r.body, *out))
            return true;
        if (!r.error.empty())
            *error = r.error;
        else if (r.status == 401)
            *error = "TMDB refused the API key";
        else
            *error = "TMDB " + std::to_string(r.status) + " on " + path;
        if (r.error.empty() && r.status != 429 && r.status < 500)
            return false;
        usleep(400000 * (attempt + 1));
    }
    return false;
}

}  // namespace

bool ad_user(const std::string &key, std::string *user, bool *premium, std::string *error)
{
    Json d;
    if (!ad_call(key, "/v4/user", "", &d, error))
        return false;
    const Json &u = d["user"];
    *user = u["username"].text();
    *premium = u["isPremium"].yes;
    return true;
}

bool ad_ready_files(const std::string &key, std::vector<AdFile> *files, int *magnets, std::string *error)
{
    Json d;
    if (!ad_call(key, "/v4.1/magnet/status", "", &d, error))
        return false;
    std::vector<std::string> ready;
    for (const Json &m : d["magnets"].items)
        if ((int)m["statusCode"].number() == 4)
            ready.push_back(m["id"].text());
    *magnets = (int)ready.size();
    for (size_t i = 0; i < ready.size(); i += 50) {
        std::string form;
        for (size_t k = i; k < ready.size() && k < i + 50; k++)
            form += "&id%5B%5D=" + url_encode(ready[k]);
        Json f;
        if (!ad_call(key, "/v4/magnet/files", form, &f, error))
            return false;
        for (const Json &entry : f["magnets"].items) {
            std::string id = entry["id"].text();
            for (const Json &node : entry["files"].items)
                flatten(node, id, files);
        }
    }
    return true;
}

bool ad_unlock(const std::string &key, const std::string &link, std::string *url, std::string *error)
{
    if (link.empty()) {
        *error = "Empty link: nothing to unlock";
        return false;
    }
    Json d;
    if (!ad_call(key, "/v4/link/unlock", "&link=" + url_encode(link), &d, error))
        return false;
    *url = d["link"].text();
    if (url->empty()) {
        *error = "AllDebrid unlock returned no link";
        return false;
    }
    return true;
}

std::string tmdb_key()
{
    {
        auto g = db_lock();
        if (!db().tmdb_key.empty())
            return db().tmdb_key;
    }
    return HIXFLIX_TMDB_KEY;
}

bool tmdb_has_builtin_key()
{
    return HIXFLIX_TMDB_KEY[0] != 0;
}

bool tmdb_check(const std::string &key, std::string *error)
{
    Json j;
    return tmdb_get(key, "/configuration", "", &j, error);
}

bool tmdb_search(const std::string &key, bool tv, const std::string &query, std::vector<TmdbResult> *out,
                 std::string *error)
{
    Json j;
    if (!tmdb_get(key, tv ? "/search/tv" : "/search/movie", "query=" + url_encode(query), &j, error))
        return false;
    for (const Json &r : j["results"].items) {
        TmdbResult t;
        t.id = (int64_t)r["id"].number();
        t.title = r[tv ? "name" : "title"].text();
        t.poster = r["poster_path"].text();
        t.backdrop = r["backdrop_path"].text();
        t.overview = r["overview"].text();
        t.date = r[tv ? "first_air_date" : "release_date"].text();
        t.vote = r["vote_average"].number();
        out->push_back(t);
    }
    return true;
}

bool tmdb_details(const std::string &key, bool tv, int64_t id, Json *out, std::string *error)
{
    return tmdb_get(key, (tv ? "/tv/" : "/movie/") + std::to_string(id), "", out, error);
}

bool tmdb_season(const std::string &key, int64_t id, int season, Json *out, std::string *error)
{
    return tmdb_get(key, "/tv/" + std::to_string(id) + "/season/" + std::to_string(season), "", out, error);
}

/*
 * HixFlix's web services: AllDebrid (the user's own API key: the files of
 * their magnets, and direct links to play) and TMDB (titles, posters,
 * seasons and episodes). Blocking calls, for background threads (hixlib.cc).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "http.h"

/* ---- AllDebrid (api.alldebrid.com v4) ---- */

struct AdFile {
    std::string magnet_id;
    std::string filename;
    std::string link;   /* locked: ad_unlock() makes it playable */
    int64_t size;
};

/* Checks a key: the account's name and whether it's premium. */
bool ad_user(const std::string &key, std::string *user, bool *premium, std::string *error);
/* Every file of every ready magnet (statusCode 4), flattened. */
bool ad_ready_files(const std::string &key, std::vector<AdFile> *files, int *magnets, std::string *error);
/* A locked link as a direct, streamable address. Must run on the console's own
 * connection: AllDebrid refuses server addresses (NO_SERVER). */
bool ad_unlock(const std::string &key, const std::string &link, std::string *url, std::string *error);

/* ---- TMDB (api.themoviedb.org v3) ---- */

/* The key in use: the one saved in the database, else the one built in
 * (HIXFLIX_TMDB_KEY at build time), else "". A v4 read access token works too. */
std::string tmdb_key();
bool tmdb_has_builtin_key();

struct TmdbResult {
    int64_t id;
    std::string title, poster, backdrop, overview, date;
    double vote;
};

bool tmdb_check(const std::string &key, std::string *error);
bool tmdb_search(const std::string &key, bool tv, const std::string &query, std::vector<TmdbResult> *out,
                 std::string *error);
/* /tv/{id} or /movie/{id} */
bool tmdb_details(const std::string &key, bool tv, int64_t id, Json *out, std::string *error);
/* /tv/{id}/season/{n} */
bool tmdb_season(const std::string &key, int64_t id, int season, Json *out, std::string *error);

/*
 * HixFlix's local database: what HIX kept in Supabase, in one JSON file in the
 * app's folder (hixflix-db.json). The AllDebrid key, every AllDebrid file
 * link, the titles, seasons and episodes matched on TMDB (with their image
 * paths), categories, and where each title was stopped.
 *
 * Everything lives in memory; db_lock() guards it (the scanner writes from its
 * own thread). Changes are saved by db_save_soon(): written whole, through a
 * temporary file, a moment later from the main thread's db_update().
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <map>
#include <mutex>
#include <stdint.h>
#include <string>
#include <vector>

struct DbMedia {
    int id = 0;
    int64_t tmdb_id = 0;         /* 0: a custom title */
    std::string title;
    std::string type;            /* "movie", "tv" or "custom" */
    std::string poster, backdrop; /* a TMDB path ("/abc.jpg") or a full address */
    std::string overview;
    int year = 0;
    double vote = 0;
    std::string genres;          /* TMDB genre ids, "16,10759" */
    bool anime = false;
    int64_t created = 0;         /* unix time */
};

struct DbSeason {
    int id = 0, media_id = 0, number = 0;
    std::string name, overview, poster, air_date;
    int episode_count = 0;
};

struct DbEpisode {
    int id = 0, season_id = 0, media_id = 0, number = 0;
    std::string name, overview, still, air_date;
    int runtime = 0;             /* minutes */
    double vote = 0;
};

/* One video file of the AllDebrid library (HIX's stream_links). */
struct DbLink {
    int id = 0;
    int media_id = 0, episode_id = 0;  /* 0: not matched / not an episode */
    std::string magnet_id;
    std::string filename;
    std::string url;             /* the locked AllDebrid link */
    std::string quality;
    int64_t size = 0;
    bool matched = false, hidden = false;
    std::string parsed_show;
    int parsed_season = -1, parsed_episode = -1;
    std::string unlocked_url;    /* the last direct link AllDebrid gave */
    int64_t unlocked_at = 0;
    int64_t created = 0;
};

struct DbCategory {
    int id = 0;
    std::string name;
};

/* Where a title was stopped (HIX's user_media_state). */
struct DbWatch {
    int64_t position = 0, length = 0;  /* seconds */
    int link_id = 0;                   /* the file last played */
    int64_t watched_at = 0;            /* unix time, 0: never */
};

struct Db {
    std::string alldebrid_key, alldebrid_user, tmdb_key;
    std::string block_terms;           /* names to leave out of a scan, comma separated */
    int64_t last_scan = 0;
    std::vector<DbMedia> media;
    std::vector<DbSeason> seasons;
    std::vector<DbEpisode> episodes;
    std::vector<DbLink> links;
    std::vector<DbCategory> categories;
    std::vector<std::pair<int, int>> media_categories; /* (media id, category id) */
    std::map<int, DbWatch> watch;                      /* by media id */
    std::map<int, std::pair<int64_t, int64_t>> link_progress; /* by file: seconds in, of how many */
    int next_id = 1;                                   /* ids for every table */
};

void db_init();          /* reads the file (before the interface) */
void db_update();        /* main thread, every frame: saves when due */
void db_flush();         /* saves now if anything changed (quitting) */
std::unique_lock<std::recursive_mutex> db_lock();
Db &db();                /* only with db_lock() held */
void db_save_soon();
/* Bumped by every change: lists made from the database are stale. */
int db_generation();
std::string db_path();

/* ---- lookups and changes (hold db_lock()) ---- */

int db_new_id();
DbMedia *db_media(int id);
DbMedia *db_media_by_tmdb(int64_t tmdb_id, const std::string &type);
DbLink *db_link(int id);
DbSeason *db_season(int media_id, int number);
DbSeason *db_season_by_id(int id);
DbEpisode *db_episode(int season_id, int number);
DbEpisode *db_episode_by_id(int id);
/* A title is in the library while one of its files is matched and not hidden. */
bool db_media_visible(int id);
/* Removes a title that no file points to any more (and its seasons, episodes,
 * categories and watch state). */
void db_drop_orphans();
/* Forgets everything but the keys. */
void db_clear_library();

/* "https://image.tmdb.org/t/p/w342/abc.jpg" for a TMDB path; a full address as it is. */
std::string db_image_url(const std::string &path, const char *size);

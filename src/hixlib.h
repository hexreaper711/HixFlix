/*
 * HixFlix's library: what HIX's edge functions and lib/api.ts did, run on the
 * console against the local database. A scan reads the AllDebrid magnets into
 * files and matches them to TMDB; manual matching, custom titles, hiding,
 * categories and watch progress; and the unlock that turns a file into an
 * address VLC plays.
 *
 * The slow parts are jobs on threads of their own: start one, then poll its
 * status every frame.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "hixapi.h"

/* ---- file names (HIX's parser.ts) ---- */

struct ParsedName {
    std::string show;      /* "" if nothing was left */
    int season = -1, episode = -1;
    std::string quality;   /* "1080P", "4K"... */
    bool movie = true;
};
ParsedName parse_filename(const std::string &filename);
/* A parsed name as a search: no "[Group] " prefix, no trailing year. */
std::string normalize_name(const std::string &name);

/* ---- jobs ---- */

enum JobState { JOB_IDLE, JOB_BUSY, JOB_DONE, JOB_FAILED };
struct JobStatus {
    JobState state = JOB_IDLE;
    std::string message;   /* progress while busy, the outcome after */
    float progress = -1;   /* 0..1, -1 unknown */
    int serial = 0;        /* bumped by every start */
};

/* The first page: checks an AllDebrid key (and a TMDB key, if given), and
 * saves them when they work. */
void keys_check_start(const std::string &alldebrid_key, const std::string &tmdb_key);
JobStatus keys_check_status();

/* Settings > Scan: the AllDebrid files in (new ones; ones gone from AllDebrid
 * out), then every unmatched title searched on TMDB. */
void scan_start();
JobStatus scan_status();
bool scan_busy();

/* Playing a file: its direct address (a fresh one when the last is old). */
void unlock_start(int link_id, bool fresh = false);
JobStatus unlock_status();
std::string unlock_url();
int unlock_link_id();
bool unlock_was_cached();

/* Manage > Match: TMDB search, then match a title's unmatched files to a result. */
void tmdb_search_start(bool tv, const std::string &query);
JobStatus tmdb_search_status();
std::vector<TmdbResult> tmdb_search_results();
void match_tmdb_start(bool tv, int64_t tmdb_id, const std::vector<int> &link_ids);
JobStatus match_status();

/* ---- quick changes (no network; they take the database lock themselves) ---- */

/* A custom video: a flat, name-only collection; the same title again adds to it. */
int match_custom_video(const std::string &title, const std::vector<int> &link_ids, const std::string &poster);
/* A custom series (One Pace...): S/E numbers read from the names, or 1, 2, 3 in name order. */
int match_custom_series(const std::string &title, int season, const std::vector<int> &link_ids,
                        const std::string &poster);
void set_media_poster(int media_id, const std::string &url);
void set_link_hidden(int link_id, bool hidden);
void delete_link(int link_id);
void set_show_hidden(const std::string &parsed_show, bool hidden);
void delete_show(const std::string &parsed_show);
void set_media_hidden(int media_id, bool hidden);
void unmatch_media(int media_id);

int category_create(const std::string &name);   /* the existing one if the name is taken */
void category_delete(int category_id);
bool category_has(int media_id, int category_id);
void category_toggle(int media_id, int category_id);

/* Where a file was stopped (ms in, ms long); finished: back to 0, and the next
 * episode becomes the title's resume point. */
void save_progress(int link_id, int64_t ms, int64_t length_ms, bool finished);

/* The file after (step 1) or before (-1) this one: the next episode, or the
 * title's next file; 0 if none. */
int neighbour_link(int link_id, int step);
/* What to call a file in the player: "Title · S1:E3 Name". */
std::string link_title(int link_id);

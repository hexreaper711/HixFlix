/*
 * HixFlix's library, ported from HIX: scan/index.ts (ingest and auto match),
 * _shared/match.ts (TMDB metadata, manual and custom matches), parser.ts, and
 * lib/api.ts (hiding, categories, watch state, the device-side unlock).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hixlib.h"

#include <algorithm>
#include <ctype.h>
#include <map>
#include <mutex>
#include <set>
#include <stdio.h>
#include <thread>
#include <time.h>
#include <tuple>

#include "hixdb.h"

namespace {

const int64_t MIN_FILE_SIZE = 100ll * 1024 * 1024; /* smaller files: samples, extras */
const int64_t UNLOCK_KEEP = 6 * 3600;              /* a direct link is reused this long */

int64_t unix_now()
{
    return (int64_t)time(nullptr);
}

std::string lower(std::string s)
{
    for (char &c : s)
        c = (char)tolower((unsigned char)c);
    return s;
}

std::string trim(std::string s)
{
    while (!s.empty() && isspace((unsigned char)s.back()))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && isspace((unsigned char)s[i]))
        i++;
    return s.substr(i);
}

int year_of(const std::string &date)
{
    return date.size() >= 4 ? atoi(date.substr(0, 4).c_str()) : 0;
}

/* ---- jobs ---- */

struct Job {
    std::mutex m;
    JobStatus s;

    bool begin(const std::string &msg)
    {
        std::lock_guard<std::mutex> g(m);
        if (s.state == JOB_BUSY)
            return false;
        s.state = JOB_BUSY;
        s.message = msg;
        s.progress = -1;
        s.serial++;
        return true;
    }
    void say(const std::string &msg, float progress = -1)
    {
        std::lock_guard<std::mutex> g(m);
        s.message = msg;
        s.progress = progress;
    }
    void end(bool ok, const std::string &msg)
    {
        std::lock_guard<std::mutex> g(m);
        s.state = ok ? JOB_DONE : JOB_FAILED;
        s.message = msg;
        s.progress = -1;
    }
    JobStatus get()
    {
        std::lock_guard<std::mutex> g(m);
        return s;
    }
};

Job keys_job, scan_job, unlock_job, search_job, match_job;

std::mutex result_m;
std::string unlocked_url;
int unlocked_link;
bool unlocked_cached;
std::vector<TmdbResult> search_results;

/* ---- matching (match.ts), with the network outside the database lock ---- */

struct LinkRef {
    int id;
    int season, episode;
};

/* What a TV match needs from TMDB: the show, and the seasons its files are in. */
struct TvInfo {
    Json details;
    std::map<int, Json> seasons;
};

bool fetch_tv(const std::string &key, int64_t tmdb_id, const std::vector<LinkRef> &links, TvInfo *info,
              std::string *error)
{
    if (!tmdb_details(key, true, tmdb_id, &info->details, error))
        return false;
    std::set<int> needed;
    for (const LinkRef &l : links)
        if (l.season >= 0)
            needed.insert(l.season);
    for (const Json &s : info->details["seasons"].items) {
        int n = (int)s["season_number"].number(-1);
        if (!needed.count(n))
            continue;
        Json sd;
        std::string ignored;
        if (tmdb_season(key, tmdb_id, n, &sd, &ignored))
            info->seasons[n] = sd;
    }
    return true;
}

std::string genre_list(const Json &details, bool *anime)
{
    std::string g;
    *anime = false;
    for (const Json &x : details["genres"].items) {
        int id = (int)x["id"].number();
        if (id == 16) /* TMDB's Animation genre */
            *anime = true;
        if (!g.empty())
            g += ",";
        g += std::to_string(id);
    }
    return g;
}

/* upsertMedia: the title by its TMDB id, created or brought up to date. */
int upsert_media(const Json &d, bool tv)
{
    int64_t tmdb_id = (int64_t)d["id"].number();
    const char *type = tv ? "tv" : "movie";
    DbMedia *m = db_media_by_tmdb(tmdb_id, type);
    if (!m) {
        DbMedia fresh;
        fresh.id = db_new_id();
        fresh.tmdb_id = tmdb_id;
        fresh.type = type;
        fresh.created = unix_now();
        db().media.push_back(fresh);
        m = &db().media.back();
    }
    m->title = d[tv ? "name" : "title"].text();
    /* a poster the user set by hand stays */
    if (m->poster.compare(0, 4, "http") != 0) {
        m->poster = d["poster_path"].text();
        m->backdrop = d["backdrop_path"].text();
    }
    m->overview = d["overview"].text();
    m->year = year_of(d[tv ? "first_air_date" : "release_date"].text());
    m->vote = d["vote_average"].number();
    m->genres = genre_list(d, &m->anime);
    return m->id;
}

/* ensureTvMetadata + markLinksTv */
int apply_tv(const TvInfo &info, const std::vector<LinkRef> &links)
{
    int media_id = upsert_media(info.details, true);
    for (const Json &s : info.details["seasons"].items) {
        int n = (int)s["season_number"].number(-1);
        auto sd = info.seasons.find(n);
        if (sd == info.seasons.end())
            continue;
        DbSeason *season = db_season(media_id, n);
        if (!season) {
            DbSeason fresh;
            fresh.id = db_new_id();
            fresh.media_id = media_id;
            fresh.number = n;
            db().seasons.push_back(fresh);
            season = &db().seasons.back();
        }
        season->name = s["name"].text();
        season->overview = s["overview"].text();
        season->poster = s["poster_path"].text();
        season->episode_count = (int)s["episode_count"].number();
        season->air_date = s["air_date"].text();
        int season_id = season->id;
        std::set<int> needed;
        for (const LinkRef &l : links)
            if (l.season == n && l.episode >= 0)
                needed.insert(l.episode);
        for (const Json &e : sd->second["episodes"].items) {
            int en = (int)e["episode_number"].number(-1);
            if (!needed.count(en))
                continue;
            DbEpisode *ep = db_episode(season_id, en);
            if (!ep) {
                DbEpisode fresh;
                fresh.id = db_new_id();
                fresh.season_id = season_id;
                fresh.media_id = media_id;
                fresh.number = en;
                db().episodes.push_back(fresh);
                ep = &db().episodes.back();
            }
            ep->name = e["name"].text();
            ep->overview = e["overview"].text();
            ep->still = e["still_path"].text();
            ep->air_date = e["air_date"].text();
            ep->runtime = (int)e["runtime"].number();
            ep->vote = e["vote_average"].number();
        }
    }
    for (const LinkRef &ref : links) {
        DbLink *l = db_link(ref.id);
        if (!l)
            continue;
        int episode_id = 0;
        if (ref.season >= 0 && ref.episode >= 0)
            if (DbSeason *s = db_season(media_id, ref.season))
                if (DbEpisode *e = db_episode(s->id, ref.episode))
                    episode_id = e->id;
        l->matched = true;
        l->media_id = media_id;
        l->episode_id = episode_id;
    }
    return media_id;
}

int apply_movie(const Json &details, const std::vector<LinkRef> &links)
{
    int media_id = upsert_media(details, false);
    for (const LinkRef &ref : links)
        if (DbLink *l = db_link(ref.id)) {
            l->matched = true;
            l->media_id = media_id;
            l->episode_id = 0;
        }
    return media_id;
}

/* A manual or automatic TMDB match, network first, then the database. */
bool match_tmdb(const std::string &key, bool tv, int64_t tmdb_id, const std::vector<LinkRef> &links,
                std::string *title, std::string *error)
{
    if (tv) {
        TvInfo info;
        if (!fetch_tv(key, tmdb_id, links, &info, error))
            return false;
        auto g = db_lock();
        apply_tv(info, links);
        *title = info.details["name"].text();
    } else {
        Json d;
        if (!tmdb_details(key, false, tmdb_id, &d, error))
            return false;
        auto g = db_lock();
        apply_movie(d, links);
        *title = d["title"].text();
    }
    auto g = db_lock();
    db_drop_orphans();
    db_save_soon();
    return true;
}

/* Strip release-group prefixes and a trailing year before a TMDB search. */
/* a year 19xx or 20xx at s[i..i+3] */
bool is_year(const std::string &s, size_t i)
{
    return i + 4 <= s.size() && ((s[i] == '1' && s[i + 1] == '9') || (s[i] == '2' && s[i + 1] == '0')) &&
           isdigit((unsigned char)s[i + 2]) && isdigit((unsigned char)s[i + 3]);
}

std::string normalize_for_search(const std::string &name)
{
    std::string s = name;
    /* "[Group] Name" */
    if (!s.empty() && s[0] == '[') {
        size_t close = s.find(']');
        if (close != std::string::npos && close > 1) {
            s = s.substr(close + 1);
            while (!s.empty() && isspace((unsigned char)s[0]))
                s.erase(0, 1);
        }
    }
    std::string t = trim(s);
    /* "Name (2019)" */
    if (t.size() >= 6 && t.back() == ')' && t[t.size() - 6] == '(' && is_year(t, t.size() - 5))
        t = trim(t.substr(0, t.size() - 6));
    /* "Name 2019" */
    else if (t.size() >= 5 && isspace((unsigned char)t[t.size() - 5]) && is_year(t, t.size() - 4))
        t = trim(t.substr(0, t.size() - 5));
    return t;
}

std::vector<std::string> block_list()
{
    std::vector<std::string> out;
    std::string terms;
    {
        auto g = db_lock();
        terms = db().block_terms;
    }
    size_t at = 0;
    while (at <= terms.size()) {
        size_t comma = terms.find(',', at);
        std::string t = lower(trim(terms.substr(at, comma == std::string::npos ? std::string::npos : comma - at)));
        if (!t.empty())
            out.push_back(t);
        if (comma == std::string::npos)
            break;
        at = comma + 1;
    }
    return out;
}

/* ---- the scan (scan/index.ts) ---- */

void scan_run()
{
    std::string ad_key;
    {
        auto g = db_lock();
        ad_key = db().alldebrid_key;
    }
    if (ad_key.empty()) {
        scan_job.end(false, "No AllDebrid key: add it in Settings first");
        return;
    }

    /* Phase 1: ingest */
    scan_job.say("Reading your AllDebrid magnets…");
    std::vector<AdFile> files;
    int magnets = 0;
    std::string error;
    if (!ad_ready_files(ad_key, &files, &magnets, &error)) {
        scan_job.end(false, error);
        return;
    }
    std::vector<std::string> blocked = block_list();
    int added = 0, filtered = 0, removed = 0;
    {
        auto g = db_lock();
        std::map<std::string, size_t> known; /* file name -> its index in links */
        std::set<std::string> seen;
        for (size_t i = 0; i < db().links.size(); i++)
            known[db().links[i].filename] = i;
        for (const AdFile &f : files) {
            if (f.size < MIN_FILE_SIZE)
                continue;
            std::string low = lower(f.filename);
            if (std::any_of(blocked.begin(), blocked.end(), [&](const std::string &t) { return low.find(t) != std::string::npos; })) {
                filtered++;
                continue;
            }
            seen.insert(f.filename);
            auto k = known.find(f.filename);
            if (k != known.end()) {
                /* the same file in a magnet added again: its new link */
                DbLink &l = db().links[k->second];
                if (l.url != f.link) {
                    l.url = f.link;
                    l.magnet_id = f.magnet_id;
                    l.unlocked_url.clear();
                    l.unlocked_at = 0;
                }
                continue;
            }
            known[f.filename] = db().links.size();
            ParsedName p = parse_filename(f.filename);
            DbLink l;
            l.id = db_new_id();
            l.magnet_id = f.magnet_id;
            l.filename = f.filename;
            l.url = f.link;
            l.quality = p.quality;
            l.size = f.size;
            l.parsed_show = p.show;
            l.parsed_season = p.season;
            l.parsed_episode = p.episode;
            l.created = unix_now();
            db().links.push_back(l);
            added++;
        }
        /* Files no longer on AllDebrid can't play: out (only after a full,
         * successful read that found magnets: an empty answer removes nothing). */
        if (magnets > 0) {
            size_t before = db().links.size();
            db().links.erase(std::remove_if(db().links.begin(), db().links.end(),
                                            [&](const DbLink &l) { return !seen.count(l.filename); }),
                             db().links.end());
            removed = (int)(before - db().links.size());
            db_drop_orphans();
        }
        db().last_scan = unix_now();
        db_save_soon();
    }

    /* Phase 2: match the unmatched titles */
    std::string tkey = tmdb_key();
    if (tkey.empty()) {
        scan_job.end(true, std::to_string(added) + " new files. Add a TMDB key in Settings to match them to titles");
        return;
    }
    struct Group {
        std::string search;
        std::vector<LinkRef> links;
        bool tv = false;
        int newest = 0;
    };
    std::map<std::string, Group> groups;
    {
        auto g = db_lock();
        for (const DbLink &l : db().links) {
            if (l.matched || l.parsed_show.empty())
                continue;
            Group &gr = groups[lower(trim(l.parsed_show))];
            if (gr.search.empty())
                gr.search = normalize_for_search(l.parsed_show);
            gr.links.push_back({ l.id, l.parsed_season, l.parsed_episode });
            gr.tv = gr.tv || l.parsed_season >= 0;
            gr.newest = std::max(gr.newest, l.id);
        }
    }
    /* newest first: what was just added gets matched first */
    std::vector<Group *> order;
    for (auto &kv : groups)
        order.push_back(&kv.second);
    std::sort(order.begin(), order.end(), [](const Group *a, const Group *b) { return a->newest > b->newest; });
    int matched = 0, done = 0;
    for (Group *gr : order) {
        scan_job.say("Matching " + gr->search + "…", order.empty() ? -1 : (float)done / order.size());
        done++;
        std::vector<TmdbResult> results;
        if (gr->search.empty() || !tmdb_search(tkey, gr->tv, gr->search, &results, &error) || results.empty())
            continue;
        std::string title;
        if (match_tmdb(tkey, gr->tv, results[0].id, gr->links, &title, &error))
            matched++;
    }
    std::string msg = std::to_string(added) + " new files";
    if (removed)
        msg += ", " + std::to_string(removed) + " gone";
    msg += ". " + std::to_string(matched) + " of " + std::to_string(order.size()) + " titles matched";
    if (filtered)
        msg += " (" + std::to_string(filtered) + " filtered)";
    scan_job.end(true, msg);
}

/* the links of a title, sorted for play order */
std::vector<const DbLink *> title_links(int media_id)
{
    std::vector<const DbLink *> out;
    for (const DbLink &l : db().links)
        if (l.media_id == media_id && l.matched && !l.hidden)
            out.push_back(&l);
    auto key = [](const DbLink *l) {
        int s = 9999, e = 9999;
        if (const DbEpisode *ep = db_episode_by_id(l->episode_id)) {
            e = ep->number;
            if (const DbSeason *se = db_season_by_id(ep->season_id))
                s = se->number;
        } else if (l->parsed_season >= 0) {
            s = l->parsed_season;
            e = l->parsed_episode;
        }
        return std::make_tuple(s, e, l->filename);
    };
    std::sort(out.begin(), out.end(), [&](const DbLink *a, const DbLink *b) { return key(a) < key(b); });
    return out;
}

}  // namespace

/* ---- file names ---- */

std::string normalize_name(const std::string &name)
{
    return normalize_for_search(name);
}

namespace {

bool separator(char c)
{
    return c == '.' || c == '_' || c == '-' || isspace((unsigned char)c);
}

/* up to max digits at s[*i] (at least min); their value, or -1 */
int digits(const std::string &s, size_t *i, int min, int max)
{
    int n = 0, v = 0;
    while (*i < s.size() && n < max && isdigit((unsigned char)s[*i])) {
        v = v * 10 + (s[*i] - '0');
        (*i)++;
        n++;
    }
    return n >= min ? v : -1;
}

/* The release-name patterns HIX's parser.ts matches, leftmost first:
 * 0 [sep]S01E05, 1 [sep]S01x05, 2 [sep]1x05, 3 Season 1 Episode 5.
 * The match's start, or npos. */
size_t find_episode(const std::string &name, int kind, int *season, int *episode)
{
    std::string low = lower(name);
    for (size_t at = 0; at < low.size(); at++) {
        size_t i = at;
        int s = -1, e = -1;
        if (kind <= 2) {
            if (!separator(low[i]))
                continue;
            i++;
            if (kind <= 1) {
                if (i >= low.size() || low[i] != 's')
                    continue;
                i++;
            }
            s = digits(low, &i, 1, 2);
            char mark = kind == 0 ? 'e' : 'x';
            if (s < 0 || i >= low.size() || low[i] != mark)
                continue;
            i++;
            e = digits(low, &i, kind == 2 ? 2 : 1, 3);
        } else {
            if (low.compare(i, 6, "season") != 0)
                continue;
            i += 6;
            while (i < low.size() && isspace((unsigned char)low[i]))
                i++;
            s = digits(low, &i, 1, 2);
            while (i < low.size() && isspace((unsigned char)low[i]))
                i++;
            if (s < 0 || low.compare(i, 7, "episode") != 0)
                continue;
            i += 7;
            while (i < low.size() && isspace((unsigned char)low[i]))
                i++;
            e = digits(low, &i, 1, 3);
        }
        if (s >= 0 && e >= 0) {
            *season = s;
            *episode = e;
            return at;
        }
    }
    return std::string::npos;
}

}  // namespace

ParsedName parse_filename(const std::string &filename)
{
    auto clean = [](const std::string &raw) {
        std::string s;
        bool space = false;
        for (char c : raw) {
            if (separator(c)) {
                space = !s.empty();
                continue;
            }
            if (space)
                s += ' ';
            space = false;
            s += c;
        }
        return s;
    };
    ParsedName p;
    size_t dot = filename.rfind('.');
    std::string name = dot != std::string::npos && dot > 0 ? filename.substr(0, dot) : filename;
    std::string low = lower(name);
    size_t best = std::string::npos;
    for (const char *q : { "2160p", "1080p", "720p", "480p", "4k", "uhd" }) {
        size_t at = low.find(q);
        if (at < best) {
            best = at;
            p.quality = q;
        }
    }
    for (char &c : p.quality)
        c = (char)toupper((unsigned char)c);
    for (int kind = 0; kind < 4; kind++) {
        int s, e;
        size_t start = find_episode(name, kind, &s, &e);
        if (start == std::string::npos)
            continue;
        p.show = clean(name.substr(0, start));
        p.season = s;
        p.episode = e;
        p.movie = false;
        return p;
    }
    /* no season or episode: a film, cut at the year ([sep]19xx[sep]) */
    for (size_t i = 0; i + 5 < name.size(); i++)
        if (separator(name[i]) && is_year(name, i + 1) && separator(name[i + 5])) {
            p.show = clean(name.substr(0, i));
            return p;
        }
    p.show = clean(name);
    return p;
}

/* ---- jobs ---- */

void keys_check_start(const std::string &alldebrid_key, const std::string &tmdb)
{
    if (!keys_job.begin("Checking your AllDebrid key…"))
        return;
    std::thread([ad = trim(alldebrid_key), tk = trim(tmdb)] {
        std::string user, error;
        bool premium = false;
        if (!ad_user(ad, &user, &premium, &error)) {
            keys_job.end(false, error);
            return;
        }
        if (!tk.empty()) {
            keys_job.say("Checking your TMDB key…");
            if (!tmdb_check(tk, &error)) {
                keys_job.end(false, error);
                return;
            }
        }
        {
            auto g = db_lock();
            db().alldebrid_key = ad;
            db().alldebrid_user = user;
            if (!tk.empty())
                db().tmdb_key = tk;
            db_save_soon();
        }
        keys_job.end(true, premium ? "Signed in as " + user : "Signed in as " + user + " (not premium: links may not unlock)");
    }).detach();
}

JobStatus keys_check_status()
{
    return keys_job.get();
}

void scan_start()
{
    if (scan_job.begin("Starting the scan…"))
        std::thread(scan_run).detach();
}

JobStatus scan_status()
{
    return scan_job.get();
}

bool scan_busy()
{
    return scan_job.get().state == JOB_BUSY;
}

void unlock_start(int link_id, bool fresh)
{
    if (!unlock_job.begin("Getting the stream…"))
        return;
    std::string key, link, cached;
    {
        auto g = db_lock();
        key = db().alldebrid_key;
        if (DbLink *l = db_link(link_id)) {
            link = l->url;
            if (!fresh && !l->unlocked_url.empty() && unix_now() - l->unlocked_at < UNLOCK_KEEP)
                cached = l->unlocked_url;
        }
    }
    {
        std::lock_guard<std::mutex> g(result_m);
        unlocked_link = link_id;
        unlocked_url = cached;
        unlocked_cached = !cached.empty();
    }
    if (!cached.empty()) {
        unlock_job.end(true, "");
        return;
    }
    std::thread([key, link, link_id] {
        std::string url, error;
        if (key.empty()) {
            unlock_job.end(false, "No AllDebrid key: add it in Settings first");
            return;
        }
        if (!ad_unlock(key, link, &url, &error)) {
            unlock_job.end(false, error);
            return;
        }
        {
            auto g = db_lock();
            if (DbLink *l = db_link(link_id)) {
                l->unlocked_url = url;
                l->unlocked_at = unix_now();
                db_save_soon();
            }
        }
        {
            std::lock_guard<std::mutex> g(result_m);
            unlocked_url = url;
        }
        unlock_job.end(true, "");
    }).detach();
}

JobStatus unlock_status()
{
    return unlock_job.get();
}

std::string unlock_url()
{
    std::lock_guard<std::mutex> g(result_m);
    return unlocked_url;
}

int unlock_link_id()
{
    std::lock_guard<std::mutex> g(result_m);
    return unlocked_link;
}

bool unlock_was_cached()
{
    std::lock_guard<std::mutex> g(result_m);
    return unlocked_cached;
}

void tmdb_search_start(bool tv, const std::string &query)
{
    if (!search_job.begin("Searching TMDB…"))
        return;
    {
        std::lock_guard<std::mutex> g(result_m);
        search_results.clear();
    }
    std::thread([tv, q = trim(query)] {
        std::string key = tmdb_key(), error;
        if (key.empty()) {
            search_job.end(false, "Add a TMDB key in Settings first");
            return;
        }
        std::vector<TmdbResult> out;
        if (!tmdb_search(key, tv, q, &out, &error)) {
            search_job.end(false, error);
            return;
        }
        {
            std::lock_guard<std::mutex> g(result_m);
            search_results = out;
        }
        search_job.end(true, out.empty() ? "Nothing found on TMDB" : "");
    }).detach();
}

JobStatus tmdb_search_status()
{
    return search_job.get();
}

std::vector<TmdbResult> tmdb_search_results()
{
    std::lock_guard<std::mutex> g(result_m);
    return search_results;
}

void match_tmdb_start(bool tv, int64_t tmdb_id, const std::vector<int> &link_ids)
{
    if (!match_job.begin("Matching…"))
        return;
    std::vector<LinkRef> links;
    {
        auto g = db_lock();
        for (int id : link_ids)
            if (DbLink *l = db_link(id))
                links.push_back({ l->id, l->parsed_season, l->parsed_episode });
    }
    std::thread([tv, tmdb_id, links] {
        std::string key = tmdb_key(), title, error;
        if (key.empty()) {
            match_job.end(false, "Add a TMDB key in Settings first");
            return;
        }
        if (links.empty()) {
            match_job.end(false, "No files to match");
            return;
        }
        if (!match_tmdb(key, tv, tmdb_id, links, &title, &error)) {
            match_job.end(false, error);
            return;
        }
        match_job.end(true, "Matched " + std::to_string(links.size()) + " files to " + title);
    }).detach();
}

JobStatus match_status()
{
    return match_job.get();
}

/* ---- quick changes ---- */

int match_custom_video(const std::string &title_in, const std::vector<int> &link_ids, const std::string &poster)
{
    std::string title = trim(title_in);
    auto g = db_lock();
    DbMedia *m = nullptr;
    for (DbMedia &x : db().media)
        if (x.type == "custom" && !x.tmdb_id && x.title == title)
            m = &x;
    if (!m) {
        DbMedia fresh;
        fresh.id = db_new_id();
        fresh.title = title;
        fresh.type = "custom";
        fresh.created = unix_now();
        db().media.push_back(fresh);
        m = &db().media.back();
    }
    if (!trim(poster).empty())
        m->poster = m->backdrop = trim(poster);
    int media_id = m->id;
    for (int id : link_ids)
        if (DbLink *l = db_link(id)) {
            l->matched = true;
            l->media_id = media_id;
            l->episode_id = 0;
        }
    db_drop_orphans();
    db_save_soon();
    return media_id;
}

int match_custom_series(const std::string &title_in, int season_number, const std::vector<int> &link_ids,
                        const std::string &poster)
{
    std::string title = trim(title_in);
    auto g = db_lock();
    /* episode numbers from the names (S01E05, 1x05), else 1, 2, 3 in name order */
    std::vector<std::pair<int, int>> assign; /* (link, episode) */
    std::vector<DbLink *> links;
    for (int id : link_ids)
        if (DbLink *l = db_link(id))
            links.push_back(l);
    std::sort(links.begin(), links.end(), [](const DbLink *a, const DbLink *b) { return a->filename < b->filename; });
    bool numbered = !links.empty() && std::all_of(links.begin(), links.end(), [](const DbLink *l) { return l->parsed_episode >= 0; });
    for (size_t i = 0; i < links.size(); i++)
        assign.push_back({ links[i]->id, numbered ? links[i]->parsed_episode : (int)i + 1 });

    DbMedia *m = nullptr;
    for (DbMedia &x : db().media)
        if (x.type == "tv" && !x.tmdb_id && x.title == title)
            m = &x;
    if (!m) {
        DbMedia fresh;
        fresh.id = db_new_id();
        fresh.title = title;
        fresh.type = "tv";
        fresh.created = unix_now();
        db().media.push_back(fresh);
        m = &db().media.back();
    }
    if (!trim(poster).empty())
        m->poster = m->backdrop = trim(poster);
    int media_id = m->id;
    int max_ep = 0;
    for (auto &a : assign)
        max_ep = std::max(max_ep, a.second);
    DbSeason *s = db_season(media_id, season_number);
    if (!s) {
        DbSeason fresh;
        fresh.id = db_new_id();
        fresh.media_id = media_id;
        fresh.number = season_number;
        db().seasons.push_back(fresh);
        s = &db().seasons.back();
    }
    s->name = "Season " + std::to_string(season_number);
    s->episode_count = std::max(s->episode_count, max_ep);
    int season_id = s->id;
    for (auto &a : assign) {
        DbEpisode *e = db_episode(season_id, a.second);
        if (!e) {
            DbEpisode fresh;
            fresh.id = db_new_id();
            fresh.season_id = season_id;
            fresh.media_id = media_id;
            fresh.number = a.second;
            fresh.name = "Episode " + std::to_string(a.second);
            db().episodes.push_back(fresh);
            e = &db().episodes.back();
        }
        if (DbLink *l = db_link(a.first)) {
            l->matched = true;
            l->media_id = media_id;
            l->episode_id = e->id;
        }
    }
    db_drop_orphans();
    db_save_soon();
    return media_id;
}

void set_media_poster(int media_id, const std::string &url)
{
    auto g = db_lock();
    if (DbMedia *m = db_media(media_id)) {
        m->poster = m->backdrop = trim(url);
        db_save_soon();
    }
}

void set_link_hidden(int link_id, bool hidden)
{
    auto g = db_lock();
    if (DbLink *l = db_link(link_id)) {
        l->hidden = hidden;
        db_save_soon();
    }
}

void delete_link(int link_id)
{
    auto g = db_lock();
    db().links.erase(std::remove_if(db().links.begin(), db().links.end(), [&](const DbLink &l) { return l.id == link_id; }),
                     db().links.end());
    db_drop_orphans();
    db_save_soon();
}

void set_show_hidden(const std::string &parsed_show, bool hidden)
{
    auto g = db_lock();
    for (DbLink &l : db().links)
        if (l.parsed_show == parsed_show)
            l.hidden = hidden;
    db_save_soon();
}

void delete_show(const std::string &parsed_show)
{
    auto g = db_lock();
    db().links.erase(std::remove_if(db().links.begin(), db().links.end(),
                                    [&](const DbLink &l) { return l.parsed_show == parsed_show; }),
                     db().links.end());
    db_drop_orphans();
    db_save_soon();
}

void set_media_hidden(int media_id, bool hidden)
{
    auto g = db_lock();
    for (DbLink &l : db().links)
        if (l.media_id == media_id)
            l.hidden = hidden;
    db_save_soon();
}

void unmatch_media(int media_id)
{
    auto g = db_lock();
    for (DbLink &l : db().links)
        if (l.media_id == media_id) {
            l.matched = false;
            l.media_id = 0;
            l.episode_id = 0;
        }
    db_drop_orphans();
    db_save_soon();
}

int category_create(const std::string &name_in)
{
    std::string name = trim(name_in);
    if (name.empty())
        return 0;
    auto g = db_lock();
    for (const DbCategory &c : db().categories)
        if (lower(c.name) == lower(name))
            return c.id;
    DbCategory c{ db_new_id(), name };
    db().categories.push_back(c);
    std::sort(db().categories.begin(), db().categories.end(),
              [](const DbCategory &a, const DbCategory &b) { return lower(a.name) < lower(b.name); });
    db_save_soon();
    return c.id;
}

void category_delete(int category_id)
{
    auto g = db_lock();
    auto &cs = db().categories;
    cs.erase(std::remove_if(cs.begin(), cs.end(), [&](const DbCategory &c) { return c.id == category_id; }), cs.end());
    auto &mc = db().media_categories;
    mc.erase(std::remove_if(mc.begin(), mc.end(), [&](const std::pair<int, int> &p) { return p.second == category_id; }),
             mc.end());
    db_save_soon();
}

bool category_has(int media_id, int category_id)
{
    auto g = db_lock();
    for (const auto &p : db().media_categories)
        if (p.first == media_id && p.second == category_id)
            return true;
    return false;
}

void category_toggle(int media_id, int category_id)
{
    auto g = db_lock();
    auto &mc = db().media_categories;
    auto it = std::find(mc.begin(), mc.end(), std::make_pair(media_id, category_id));
    if (it != mc.end())
        mc.erase(it);
    else
        mc.push_back({ media_id, category_id });
    db_save_soon();
}

void save_progress(int link_id, int64_t ms, int64_t length_ms, bool finished)
{
    auto g = db_lock();
    DbLink *l = db_link(link_id);
    if (!l || !l->media_id)
        return;
    int64_t pos = ms / 1000, len = length_ms / 1000;
    /* the last minutes (credits): watched */
    if (len > 0 && pos > len - std::min<int64_t>(120, len / 20))
        finished = true;
    DbWatch &w = db().watch[l->media_id];
    w.watched_at = unix_now();
    if (finished) {
        db().link_progress[link_id] = { 0, len };
        int next = neighbour_link(link_id, 1);
        w.link_id = next ? next : link_id;
        w.position = 0;
        w.length = 0;
    } else {
        db().link_progress[link_id] = { pos, len };
        w.link_id = link_id;
        w.position = pos;
        w.length = len;
    }
    db_save_soon();
}

int neighbour_link(int link_id, int step)
{
    auto g = db_lock();
    DbLink *l = db_link(link_id);
    if (!l)
        return 0;
    std::vector<const DbLink *> list = title_links(l->media_id);
    for (size_t i = 0; i < list.size(); i++)
        if (list[i]->id == link_id) {
            long j = (long)i + step;
            return j >= 0 && j < (long)list.size() ? list[j]->id : 0;
        }
    return 0;
}

std::string link_title(int link_id)
{
    auto g = db_lock();
    DbLink *l = db_link(link_id);
    if (!l)
        return "";
    DbMedia *m = db_media(l->media_id);
    std::string t = m ? m->title : l->filename;
    if (DbEpisode *e = db_episode_by_id(l->episode_id)) {
        DbSeason *s = db_season_by_id(e->season_id);
        t += " \xC2\xB7 S" + std::to_string(s ? s->number : 0) + ":E" + std::to_string(e->number);
        if (!e->name.empty())
            t += " " + e->name;
    } else if (m && m->type != "movie") {
        t += " \xC2\xB7 " + l->filename;
    }
    return t;
}

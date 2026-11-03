/*
 * HixFlix's local database: hixflix-db.json, read once at start, written whole
 * (through a temporary file, on a thread of its own) a second after changes.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hixdb.h"

#include <algorithm>
#include <set>
#include <stdio.h>
#include <thread>
#include <time.h>

#include "http.h"
#include "platform.h"

namespace {

std::recursive_mutex mtx;
std::mutex write_mtx; /* one write at a time, in order */
Db data;
int generation = 1;
bool dirty;
double dirty_since;

/* ---- writing ---- */

struct Out {
    std::string s;
    bool first = true;

    void open(char c) { s += c; first = true; }
    void close(char c) { s += c; first = false; }
    void key(const char *k)
    {
        if (!first)
            s += ',';
        first = false;
        s += '"';
        s += k;
        s += "\":";
    }
    void item()
    {
        if (!first)
            s += ',';
        first = false;
    }
    void str(const char *k, const std::string &v)
    {
        if (v.empty())
            return;
        key(k);
        s += '"' + json_escape(v) + '"';
    }
    void num(const char *k, int64_t v)
    {
        if (!v)
            return;
        key(k);
        s += std::to_string(v);
    }
    void real(const char *k, double v)
    {
        if (v == 0)
            return;
        key(k);
        char b[32];
        snprintf(b, sizeof b, "%.3f", v);
        s += b;
    }
    void flag(const char *k, bool v)
    {
        if (!v)
            return;
        key(k);
        s += "true";
    }
};

std::string serialize(const Db &d)
{
    Out o;
    o.open('{');
    o.num("version", 1);
    o.num("next_id", d.next_id);
    o.key("settings");
    o.open('{');
    o.str("alldebrid_key", d.alldebrid_key);
    o.str("alldebrid_user", d.alldebrid_user);
    o.str("tmdb_key", d.tmdb_key);
    o.str("block_terms", d.block_terms);
    o.num("last_scan", d.last_scan);
    o.close('}');

    o.key("media");
    o.open('[');
    for (const DbMedia &m : d.media) {
        o.item();
        o.open('{');
        o.num("id", m.id);
        o.num("tmdb_id", m.tmdb_id);
        o.str("title", m.title);
        o.str("type", m.type);
        o.str("poster", m.poster);
        o.str("backdrop", m.backdrop);
        o.str("overview", m.overview);
        o.num("year", m.year);
        o.real("vote", m.vote);
        o.str("genres", m.genres);
        o.flag("anime", m.anime);
        o.num("created", m.created);
        o.close('}');
    }
    o.close(']');

    o.key("seasons");
    o.open('[');
    for (const DbSeason &s : d.seasons) {
        o.item();
        o.open('{');
        o.num("id", s.id);
        o.num("media_id", s.media_id);
        o.key("number");
        o.s += std::to_string(s.number); /* season 0 (specials) is a real number */
        o.str("name", s.name);
        o.str("overview", s.overview);
        o.str("poster", s.poster);
        o.str("air_date", s.air_date);
        o.num("episode_count", s.episode_count);
        o.close('}');
    }
    o.close(']');

    o.key("episodes");
    o.open('[');
    for (const DbEpisode &e : d.episodes) {
        o.item();
        o.open('{');
        o.num("id", e.id);
        o.num("season_id", e.season_id);
        o.num("media_id", e.media_id);
        o.key("number");
        o.s += std::to_string(e.number);
        o.str("name", e.name);
        o.str("overview", e.overview);
        o.str("still", e.still);
        o.str("air_date", e.air_date);
        o.num("runtime", e.runtime);
        o.real("vote", e.vote);
        o.close('}');
    }
    o.close(']');

    o.key("links");
    o.open('[');
    for (const DbLink &l : d.links) {
        o.item();
        o.open('{');
        o.num("id", l.id);
        o.num("media_id", l.media_id);
        o.num("episode_id", l.episode_id);
        o.str("magnet_id", l.magnet_id);
        o.str("filename", l.filename);
        o.str("url", l.url);
        o.str("quality", l.quality);
        o.num("size", l.size);
        o.flag("matched", l.matched);
        o.flag("hidden", l.hidden);
        o.str("parsed_show", l.parsed_show);
        o.key("parsed_season");
        o.s += std::to_string(l.parsed_season);
        o.key("parsed_episode");
        o.s += std::to_string(l.parsed_episode);
        o.str("unlocked_url", l.unlocked_url);
        o.num("unlocked_at", l.unlocked_at);
        o.num("created", l.created);
        o.close('}');
    }
    o.close(']');

    o.key("categories");
    o.open('[');
    for (const DbCategory &c : d.categories) {
        o.item();
        o.open('{');
        o.num("id", c.id);
        o.str("name", c.name);
        o.close('}');
    }
    o.close(']');

    o.key("media_categories");
    o.open('[');
    for (const auto &mc : d.media_categories) {
        o.item();
        o.s += "[" + std::to_string(mc.first) + "," + std::to_string(mc.second) + "]";
    }
    o.close(']');

    o.key("watch");
    o.open('[');
    for (const auto &w : d.watch) {
        o.item();
        o.open('{');
        o.num("media_id", w.first);
        o.num("position", w.second.position);
        o.num("length", w.second.length);
        o.num("link_id", w.second.link_id);
        o.num("watched_at", w.second.watched_at);
        o.close('}');
    }
    o.close(']');

    o.key("link_progress");
    o.open('[');
    for (const auto &p : d.link_progress) {
        o.item();
        o.s += "[" + std::to_string(p.first) + "," + std::to_string(p.second.first) + "," +
               std::to_string(p.second.second) + "]";
    }
    o.close(']');
    o.close('}');
    o.s += '\n';
    return o.s;
}

void write_file(std::string text)
{
    std::lock_guard<std::mutex> g(write_mtx);
    std::string path = db_path(), tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) {
        fprintf(stderr, "db: can't write %s\n", tmp.c_str());
        return;
    }
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = fclose(f) == 0 && ok;
    if (ok)
        rename(tmp.c_str(), path.c_str());
    else
        fprintf(stderr, "db: writing %s failed\n", tmp.c_str());
}

/* ---- reading ---- */

int64_t I(const Json &o, const char *k, int64_t fallback = 0)
{
    return (int64_t)o[k].number((double)fallback);
}

void load(const Json &j)
{
    Db d;
    d.next_id = (int)I(j, "next_id", 1);
    const Json &st = j["settings"];
    d.alldebrid_key = st["alldebrid_key"].text();
    d.alldebrid_user = st["alldebrid_user"].text();
    d.tmdb_key = st["tmdb_key"].text();
    d.block_terms = st["block_terms"].text();
    d.last_scan = I(st, "last_scan");
    for (const Json &o : j["media"].items) {
        DbMedia m;
        m.id = (int)I(o, "id");
        m.tmdb_id = I(o, "tmdb_id");
        m.title = o["title"].text();
        m.type = o["type"].text("movie");
        m.poster = o["poster"].text();
        m.backdrop = o["backdrop"].text();
        m.overview = o["overview"].text();
        m.year = (int)I(o, "year");
        m.vote = o["vote"].number();
        m.genres = o["genres"].text();
        m.anime = o["anime"].yes;
        m.created = I(o, "created");
        d.media.push_back(m);
    }
    for (const Json &o : j["seasons"].items) {
        DbSeason s;
        s.id = (int)I(o, "id");
        s.media_id = (int)I(o, "media_id");
        s.number = (int)I(o, "number");
        s.name = o["name"].text();
        s.overview = o["overview"].text();
        s.poster = o["poster"].text();
        s.air_date = o["air_date"].text();
        s.episode_count = (int)I(o, "episode_count");
        d.seasons.push_back(s);
    }
    for (const Json &o : j["episodes"].items) {
        DbEpisode e;
        e.id = (int)I(o, "id");
        e.season_id = (int)I(o, "season_id");
        e.media_id = (int)I(o, "media_id");
        e.number = (int)I(o, "number");
        e.name = o["name"].text();
        e.overview = o["overview"].text();
        e.still = o["still"].text();
        e.air_date = o["air_date"].text();
        e.runtime = (int)I(o, "runtime");
        e.vote = o["vote"].number();
        d.episodes.push_back(e);
    }
    for (const Json &o : j["links"].items) {
        DbLink l;
        l.id = (int)I(o, "id");
        l.media_id = (int)I(o, "media_id");
        l.episode_id = (int)I(o, "episode_id");
        l.magnet_id = o["magnet_id"].text();
        l.filename = o["filename"].text();
        l.url = o["url"].text();
        l.quality = o["quality"].text();
        l.size = I(o, "size");
        l.matched = o["matched"].yes;
        l.hidden = o["hidden"].yes;
        l.parsed_show = o["parsed_show"].text();
        l.parsed_season = (int)I(o, "parsed_season", -1);
        l.parsed_episode = (int)I(o, "parsed_episode", -1);
        l.unlocked_url = o["unlocked_url"].text();
        l.unlocked_at = I(o, "unlocked_at");
        l.created = I(o, "created");
        d.links.push_back(l);
    }
    for (const Json &o : j["categories"].items)
        d.categories.push_back({ (int)I(o, "id"), o["name"].text() });
    for (const Json &o : j["media_categories"].items)
        if (o.kind == Json::ARR && o.items.size() == 2)
            d.media_categories.push_back({ (int)o.items[0].number(), (int)o.items[1].number() });
    for (const Json &o : j["watch"].items) {
        DbWatch w;
        w.position = I(o, "position");
        w.length = I(o, "length");
        w.link_id = (int)I(o, "link_id");
        w.watched_at = I(o, "watched_at");
        d.watch[(int)I(o, "media_id")] = w;
    }
    for (const Json &o : j["link_progress"].items)
        if (o.kind == Json::ARR && o.items.size() == 3)
            d.link_progress[(int)o.items[0].number()] = { (int64_t)o.items[1].number(), (int64_t)o.items[2].number() };
    /* never hand out an id that's taken (a file edited by hand) */
    auto see = [&](int id) { d.next_id = std::max(d.next_id, id + 1); };
    for (auto &x : d.media) see(x.id);
    for (auto &x : d.seasons) see(x.id);
    for (auto &x : d.episodes) see(x.id);
    for (auto &x : d.links) see(x.id);
    for (auto &x : d.categories) see(x.id);
    data = std::move(d);
}

}  // namespace

std::string db_path()
{
    return std::string(plat_data_dir()) + "/hixflix-db.json";
}

void db_init()
{
    std::lock_guard<std::recursive_mutex> g(mtx);
    FILE *f = fopen(db_path().c_str(), "rb");
    if (!f) {
        fprintf(stderr, "db: no %s yet: a new library\n", db_path().c_str());
        return;
    }
    std::string text;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        text.append(buf, n);
    fclose(f);
    Json j;
    if (!parse_json(text, j) || j.kind != Json::OBJ) {
        /* kept aside, not overwritten: a hand edit gone wrong can be fixed */
        std::string aside = db_path() + ".broken";
        rename(db_path().c_str(), aside.c_str());
        fprintf(stderr, "db: %s couldn't be read: moved to %s\n", db_path().c_str(), aside.c_str());
        return;
    }
    load(j);
    fprintf(stderr, "db: %zu titles, %zu files\n", data.media.size(), data.links.size());
}

std::unique_lock<std::recursive_mutex> db_lock()
{
    return std::unique_lock<std::recursive_mutex>(mtx);
}

Db &db()
{
    return data;
}

void db_save_soon()
{
    std::lock_guard<std::recursive_mutex> g(mtx);
    generation++;
    if (!dirty)
        dirty_since = plat_time();
    dirty = true;
}

int db_generation()
{
    std::lock_guard<std::recursive_mutex> g(mtx);
    return generation;
}

void db_update()
{
    std::string text;
    {
        std::lock_guard<std::recursive_mutex> g(mtx);
        if (!dirty || plat_time() - dirty_since < 1.0)
            return;
        dirty = false;
        text = serialize(data);
    }
    std::thread(write_file, std::move(text)).detach();
}

void db_flush()
{
    std::string text;
    {
        std::lock_guard<std::recursive_mutex> g(mtx);
        if (!dirty)
            return;
        dirty = false;
        text = serialize(data);
    }
    write_file(std::move(text));
}

int db_new_id()
{
    return data.next_id++;
}

DbMedia *db_media(int id)
{
    for (DbMedia &m : data.media)
        if (m.id == id)
            return &m;
    return nullptr;
}

DbMedia *db_media_by_tmdb(int64_t tmdb_id, const std::string &type)
{
    for (DbMedia &m : data.media)
        if (m.tmdb_id == tmdb_id && m.type == type)
            return &m;
    return nullptr;
}

DbLink *db_link(int id)
{
    for (DbLink &l : data.links)
        if (l.id == id)
            return &l;
    return nullptr;
}

DbSeason *db_season(int media_id, int number)
{
    for (DbSeason &s : data.seasons)
        if (s.media_id == media_id && s.number == number)
            return &s;
    return nullptr;
}

DbSeason *db_season_by_id(int id)
{
    for (DbSeason &s : data.seasons)
        if (s.id == id)
            return &s;
    return nullptr;
}

DbEpisode *db_episode(int season_id, int number)
{
    for (DbEpisode &e : data.episodes)
        if (e.season_id == season_id && e.number == number)
            return &e;
    return nullptr;
}

DbEpisode *db_episode_by_id(int id)
{
    for (DbEpisode &e : data.episodes)
        if (e.id == id)
            return &e;
    return nullptr;
}

bool db_media_visible(int id)
{
    for (const DbLink &l : data.links)
        if (l.media_id == id && l.matched && !l.hidden)
            return true;
    return false;
}

void db_drop_orphans()
{
    std::set<int> used;
    for (const DbLink &l : data.links)
        if (l.media_id)
            used.insert(l.media_id);
    auto gone = [&](int media_id) { return !used.count(media_id); };
    data.media.erase(std::remove_if(data.media.begin(), data.media.end(),
                                    [&](const DbMedia &m) { return gone(m.id); }),
                     data.media.end());
    data.seasons.erase(std::remove_if(data.seasons.begin(), data.seasons.end(),
                                      [&](const DbSeason &s) { return gone(s.media_id); }),
                       data.seasons.end());
    data.episodes.erase(std::remove_if(data.episodes.begin(), data.episodes.end(),
                                       [&](const DbEpisode &e) { return gone(e.media_id); }),
                        data.episodes.end());
    data.media_categories.erase(std::remove_if(data.media_categories.begin(), data.media_categories.end(),
                                               [&](const std::pair<int, int> &mc) { return gone(mc.first); }),
                                data.media_categories.end());
    for (auto it = data.watch.begin(); it != data.watch.end();)
        it = gone(it->first) ? data.watch.erase(it) : std::next(it);
    std::set<int> links;
    for (const DbLink &l : data.links)
        links.insert(l.id);
    for (auto it = data.link_progress.begin(); it != data.link_progress.end();)
        it = links.count(it->first) ? std::next(it) : data.link_progress.erase(it);
}

void db_clear_library()
{
    Db fresh;
    fresh.alldebrid_key = data.alldebrid_key;
    fresh.alldebrid_user = data.alldebrid_user;
    fresh.tmdb_key = data.tmdb_key;
    fresh.block_terms = data.block_terms;
    fresh.next_id = data.next_id; /* ids never come back: a stale one finds nothing */
    data = std::move(fresh);
}

std::string db_image_url(const std::string &path, const char *size)
{
    if (path.empty())
        return "";
    if (path.compare(0, 7, "http://") == 0 || path.compare(0, 8, "https://") == 0)
        return path;
    return std::string("https://image.tmdb.org/t/p/") + size + path;
}

/*
 * HixFlix's pictures from the web: a few downloader threads (newest ask
 * first), the files kept in <app>/cache/img, stb_image to decode them, and
 * the main thread making textures of what's ready.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "hiximg.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <thread>
#include <unordered_map>
#include <vector>

#include "http.h"
#include "library.h"
#include "platform.h"
#include "stb_image.h"

namespace {

enum State { QUEUED, LOADING, DECODED, READY, FAILED };

struct Entry {
    State state = QUEUED;
    std::vector<uint8_t> bgra;
    uint32_t w = 0, h = 0;
    ImTextureID tex = 0;
    double used = 0;
    double failed_at = 0;
};

const int WORKERS = 4;
const size_t MAX_QUEUE = 160;      /* older asks beyond this are dropped (scrolled past) */
const size_t KEEP_TEXTURES = 220;  /* more than this: the ones unseen longest go */
const uint32_t MAX_WIDTH = 1280;

/* never destroyed: the downloaders wait on these until the process ends, and
 * destroying a condition variable with waiters blocks the exit for good */
std::mutex &m = *new std::mutex;
std::condition_variable &wake = *new std::condition_variable;
std::unordered_map<std::string, Entry> &entries = *new std::unordered_map<std::string, Entry>;
std::deque<std::string> &queue = *new std::deque<std::string>; /* newest at the back */
bool started;
std::string cache_dir;

std::string cache_path(const std::string &url)
{
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : url)
        h = (h ^ c) * 1099511628211ULL;
    char name[32];
    snprintf(name, sizeof name, "%016llx.img", (unsigned long long)h);
    return cache_dir + "/" + name;
}

bool read_file(const std::string &path, std::string &out)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        out.append(buf, n);
    fclose(f);
    return !out.empty();
}

void write_file(const std::string &path, const std::string &data)
{
    std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f)
        return;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = fclose(f) == 0 && ok;
    if (ok)
        rename(tmp.c_str(), path.c_str());
}

/* RGBA from stb as BGRA, halved until it's at most MAX_WIDTH across. */
bool decode(const std::string &bytes, std::vector<uint8_t> &bgra, uint32_t &w, uint32_t &h)
{
    int iw, ih, n;
    unsigned char *rgba = stbi_load_from_memory((const unsigned char *)bytes.data(), (int)bytes.size(), &iw, &ih, &n, 4);
    if (!rgba)
        return false;
    w = (uint32_t)iw;
    h = (uint32_t)ih;
    bgra.assign(rgba, rgba + (size_t)w * h * 4);
    stbi_image_free(rgba);
    for (size_t i = 0; i < bgra.size(); i += 4)
        std::swap(bgra[i], bgra[i + 2]);
    while (w > MAX_WIDTH) {
        uint32_t nw = w / 2, nh = std::max(1u, h / 2);
        std::vector<uint8_t> out((size_t)nw * nh * 4);
        for (uint32_t y = 0; y < nh; y++)
            for (uint32_t x = 0; x < nw; x++)
                for (int c = 0; c < 4; c++) {
                    const uint8_t *a = &bgra[((size_t)(2 * y) * w + 2 * x) * 4 + c];
                    const uint8_t *b = &bgra[((size_t)std::min(2 * y + 1, h - 1) * w + 2 * x) * 4 + c];
                    out[((size_t)y * nw + x) * 4 + c] = (uint8_t)((a[0] + a[4] + b[0] + b[4] + 2) / 4);
                }
        bgra.swap(out);
        w = nw;
        h = nh;
    }
    return true;
}

void worker()
{
    for (;;) {
        std::string url;
        {
            std::unique_lock<std::mutex> g(m);
            wake.wait(g, [] { return !queue.empty(); });
            url = queue.back();
            queue.pop_back();
            auto it = entries.find(url);
            if (it == entries.end() || it->second.state != QUEUED)
                continue;
            it->second.state = LOADING;
        }
        std::string path = cache_path(url), bytes;
        bool ok = read_file(path, bytes);
        if (!ok) {
            HttpReply r = http_request("GET", url);
            ok = r.error.empty() && r.status == 200 && !r.body.empty();
            if (ok) {
                bytes.swap(r.body);
                write_file(path, bytes);
            } else {
                fprintf(stderr, "img: %s: %s\n", url.c_str(), r.error.empty() ? std::to_string(r.status).c_str() : r.error.c_str());
            }
        }
        std::vector<uint8_t> bgra;
        uint32_t w = 0, h = 0;
        if (ok && !decode(bytes, bgra, w, h)) {
            ok = false;
            remove(path.c_str()); /* not a picture: asked again next time */
        }
        std::lock_guard<std::mutex> g(m);
        auto it = entries.find(url);
        if (it == entries.end())
            continue;
        if (ok) {
            it->second.bgra.swap(bgra);
            it->second.w = w;
            it->second.h = h;
            it->second.state = DECODED;
        } else {
            it->second.state = FAILED;
            it->second.failed_at = plat_time();
        }
    }
}

}  // namespace

void hiximg_init()
{
    if (started)
        return;
    started = true;
    std::string cache = std::string(plat_data_dir()) + "/cache";
    mkdir(cache.c_str(), 0777);
    cache_dir = cache + "/img";
    mkdir(cache_dir.c_str(), 0777);
    for (int i = 0; i < WORKERS; i++)
        std::thread(worker).detach();
}

ImTextureID hix_image(const std::string &url, float *aspect)
{
    if (url.empty())
        return 0;
    double now = plat_time();
    std::lock_guard<std::mutex> g(m);
    auto it = entries.find(url);
    if (it == entries.end()) {
        Entry &e = entries[url];
        e.used = now;
        queue.push_back(url);
        if (queue.size() > MAX_QUEUE) {
            std::string old = queue.front();
            queue.pop_front();
            auto o = entries.find(old);
            if (o != entries.end() && o->second.state == QUEUED)
                entries.erase(o); /* asked again if it's drawn again */
        }
        wake.notify_one();
        return 0;
    }
    Entry &e = it->second;
    e.used = now;
    if (e.state == READY) {
        if (aspect)
            *aspect = (float)e.w / (float)(e.h ? e.h : 1);
        return e.tex;
    }
    if (e.state == FAILED && now - e.failed_at > 30) {
        /* a network hiccup shouldn't lose a poster for good: once more */
        e.state = QUEUED;
        queue.push_back(url);
        wake.notify_one();
        return 0;
    }
    if (e.state == QUEUED) {
        /* still waiting: to the front of the line (it's on screen now) */
        for (auto q = queue.begin(); q != queue.end(); ++q)
            if (*q == url) {
                queue.erase(q);
                break;
            }
        queue.push_back(url);
    }
    return 0;
}

bool hix_image_failed(const std::string &url)
{
    std::lock_guard<std::mutex> g(m);
    auto it = entries.find(url);
    return it != entries.end() && it->second.state == FAILED;
}

void hiximg_update()
{
    double now = plat_time();
    /* a few new textures a frame: each is an upload the frame waits for */
    for (int made = 0; made < 3; made++) {
        std::string url;
        std::vector<uint8_t> bgra;
        uint32_t w = 0, h = 0;
        {
            std::lock_guard<std::mutex> g(m);
            for (auto &kv : entries)
                if (kv.second.state == DECODED) {
                    url = kv.first;
                    bgra.swap(kv.second.bgra);
                    w = kv.second.w;
                    h = kv.second.h;
                    kv.second.state = LOADING; /* being uploaded */
                    break;
                }
        }
        if (url.empty())
            break;
        ImTextureID tex = library_texture_bgra(std::move(bgra), w, h);
        std::lock_guard<std::mutex> g(m);
        Entry &e = entries[url];
        e.tex = tex;
        e.state = tex ? READY : FAILED;
    }
    /* too many textures: the ones not drawn for longest go (asked again if drawn) */
    static double checked;
    if (now - checked < 1)
        return;
    checked = now;
    std::vector<std::pair<double, std::string>> ready;
    {
        std::lock_guard<std::mutex> g(m);
        for (auto &kv : entries)
            if (kv.second.state == READY)
                ready.push_back({ kv.second.used, kv.first });
    }
    if (ready.size() <= KEEP_TEXTURES)
        return;
    std::sort(ready.begin(), ready.end());
    for (size_t i = 0; i < ready.size() - KEEP_TEXTURES; i++) {
        if (now - ready[i].first < 2)
            break; /* on screen now: keep */
        ImTextureID tex = 0;
        {
            std::lock_guard<std::mutex> g(m);
            auto it = entries.find(ready[i].second);
            if (it == entries.end())
                continue;
            tex = it->second.tex;
            entries.erase(it);
        }
        library_free_image(tex);
    }
}

/*
 * HixFlix's interface, in the look of Netflix: black, red and big pictures.
 * It takes the place of VLC's library (the player, its menus and the
 * on-screen keyboard are VLC-PS5's own) and shows the AllDebrid library that
 * the scan matched to TMDB (hixlib.cc, hixdb.cc).
 *
 *   first page   the AllDebrid API key (and a TMDB key), typed or sent from a phone
 *   home         a billboard of the title in focus, rows of posters under it;
 *                the tabs (TV Shows, Movies, Anime, Custom, My List) the same
 *                with rows by genre
 *   search, a title's page (seasons, episodes, files), settings, and Manage
 *   (HIX's: match files to TMDB, custom titles, hide, delete)
 *
 * Included by ui.cc inside its namespace: it uses its drawing, input and
 * player functions.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* ---- look ------------------------------------------------------------------ */

#define HX_KEY_FILE "hixflix-keys.txt"

const ImU32 NF_BG = IM_COL32(20, 20, 20, 255);
const ImU32 NF_RED = IM_COL32(229, 9, 20, 255);
const ImU32 NF_WHITE = IM_COL32(255, 255, 255, 255);
const ImU32 NF_DIM = IM_COL32(179, 179, 179, 255);
const ImU32 NF_FAINT = IM_COL32(118, 118, 118, 255);
const ImU32 NF_CARD = IM_COL32(38, 38, 38, 255);
const ImU32 NF_RAISED = IM_COL32(51, 51, 51, 255);
const ImU32 NF_GREEN = IM_COL32(70, 211, 105, 255);
const ImU32 NF_BLACK = IM_COL32(0, 0, 0, 255);

/* Text as it is, not looked up for a translation (titles, file names). */
ImVec2 hx_size(ImFont *f, float size, const std::string &s)
{
    ImVec2 v = f->CalcTextSizeA(size * S, FLT_MAX, 0, lang_shown_as_is(s.c_str()));
    return ImVec2(v.x / S, v.y / S);
}

void hx_text(ImFont *f, float size, float x, float y, ImU32 col, const std::string &str, float align = 0,
             float max_w = 0)
{
    std::string s = lang_shown_as_is(str.c_str());
    auto width = [&](const std::string &t) { return f->CalcTextSizeA(size * S, FLT_MAX, 0, t.c_str()).x / S; };
    if (max_w > 0 && width(s) > max_w) {
        while (!s.empty() && width(s + "\xE2\x80\xA6") > max_w) {
            while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80)
                s.pop_back();
            if (!s.empty())
                s.pop_back();
        }
        s += "\xE2\x80\xA6";
    }
    float w = align ? width(s) : 0;
    dl->AddText(f, size * S, P(x - w * align, y), col, s.c_str());
}

/* Lines cut between words, the last one with an ellipsis when it goes on;
 * returns how many lines were drawn. */
int hx_wrapped(ImFont *f, float size, float x, float y, ImU32 col, const std::string &s, float max_w, int max_lines,
               float line_h = 1.42f)
{
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    bool more = false;
    while (i < s.size()) {
        size_t j = s.find(' ', i);
        if (j == std::string::npos)
            j = s.size();
        std::string word = s.substr(i, j - i);
        std::string with = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && hx_size(f, size, with).x > max_w) {
            lines.push_back(cur);
            cur = word;
            if ((int)lines.size() == max_lines) {
                more = true;
                break;
            }
        } else {
            cur = with;
        }
        i = j + 1;
    }
    if (!more && !cur.empty())
        lines.push_back(cur);
    if ((int)lines.size() > max_lines)
        lines.resize(max_lines);
    for (size_t k = 0; k < lines.size(); k++) {
        std::string l = lines[k];
        if (more && k + 1 == lines.size())
            l += " \xE2\x80\xA6";
        hx_text(f, size, x, y + k * size * line_h, col, l, 0, max_w);
    }
    return (int)lines.size();
}

/* A picture filling the box, cropped to keep its shape; false while it's coming. */
bool hx_cover(const std::string &url, float x, float y, float w, float h, float a, float round = 0,
              ImDrawFlags corners = 0)
{
    float aspect = 1;
    ImTextureID tex = hix_image(url, &aspect);
    if (!tex)
        return false;
    float box = w / h;
    ImVec2 uv0(0, 0), uv1(1, 1);
    if (aspect > box) {
        float k = box / aspect;
        uv0.x = (1 - k) / 2;
        uv1.x = 1 - uv0.x;
    } else {
        float k = aspect / box;
        uv0.y = (1 - k) / 2;
        uv1.y = 1 - uv0.y;
    }
    dl->AddImageRounded(tex, P(x, y), P(x + w, y + h), uv0, uv1, alpha(NF_WHITE, a), round * S, corners);
    return true;
}

/* gradients: across (left colour to right) and down */
void hgrad(float x, float y, float w, float h, ImU32 l, ImU32 r)
{
    dl->AddRectFilledMultiColor(P(x, y), P(x + w, y + h), l, r, r, l);
}

void dgrad(float x, float y, float w, float h, ImU32 top, ImU32 bottom)
{
    dl->AddRectFilledMultiColor(P(x, y), P(x + w, y + h), top, top, bottom, bottom);
}

/* The wordmark: red letters, a little condensed. */
void hx_logo(float x, float y, float size, float a = 1, float align = 0)
{
    const char *word = "HIXFLIX";
    float spacing = -size * 0.04f, w = 0;
    for (const char *c = word; *c; c++)
        w += hx_size(f_bold, size, std::string(1, *c)).x + spacing;
    float cx = x - w * align;
    for (const char *c = word; *c; c++) {
        std::string ch(1, *c);
        /* a slight smile, like the real one: the middle letters lower */
        float mid = (float)(c - word) - 3, dy = -(mid * mid) * size * 0.006f;
        dl->AddText(f_bold, size * S, P(cx, y + dy), alpha(NF_RED, a), ch.c_str());
        cx += hx_size(f_bold, size, ch).x + spacing;
    }
}

/* A button: white when focused (as Netflix's), grey glass otherwise. */
void hx_button(float x, float y, float w, float h, const char *label, float f, float a, int icon = 0)
{
    ImU32 fill = mix(IM_COL32(109, 109, 110, 180), NF_WHITE, f);
    ImU32 ink = mix(NF_WHITE, NF_BLACK, f);
    if (f > 0.01f)
        glow(x, y, w, h, 8, alpha(NF_WHITE, 0.25f * f * a), 14, 6);
    rect(x, y, w, h, alpha(fill, a), 8);
    float tw = text_size(f_bold, 27, label).x, iw = icon ? 36 : 0;
    float tx = x + (w - tw - iw) / 2 + iw;
    if (icon == 1)
        icon_play(tx - 22, y + h / 2, 30, alpha(ink, a));
    else if (icon == 2) {
        ring(tx - 22, y + h / 2, 13, alpha(ink, a), 2.5f);
        rect(tx - 23.5f, y + h / 2 - 2, 3, 9, alpha(ink, a), 1);
        circle(tx - 22, y + h / 2 - 7, 2, alpha(ink, a));
    } else if (icon == 3) {
        line(tx - 31, y + h / 2, tx - 13, y + h / 2, alpha(ink, a), 3);
        line(tx - 22, y + h / 2 - 9, tx - 22, y + h / 2 + 9, alpha(ink, a), 3);
    } else if (icon == 4) {
        for (int i = -1; i <= 1; i++)
            circle(tx - 22 + i * 9, y + h / 2, 3, alpha(ink, a));
    }
    text(f_bold, 27, tx, y + (h - 33) / 2, alpha(ink, a), label);
}

/* A thin red progress line on a grey track. */
void hx_progress(float x, float y, float w, float frac, float a)
{
    rect(x, y, w, 5, alpha(IM_COL32(90, 90, 90, 255), a), 0);
    rect(x, y, w * std::max(0.0f, std::min(1.0f, frac)), 5, alpha(NF_RED, a), 0);
}

/* "1 file", "3 files" */
std::string count_of(int n, const char *one, const char *many)
{
    return trf(n == 1 ? one : many, n);
}

/* ---- the library as the screens see it ---------------------------------------- */

struct HxTitle {
    int id = 0;
    std::string title, type, poster, backdrop, overview;
    std::vector<int> genres;
    int year = 0;
    double vote = 0;
    bool anime = false, custom = false;
    int64_t created = 0, watched_at = 0;
    float progress = 0;         /* of the file to resume, 0 if none */
    int resume_link = 0;
    int64_t resume_pos = 0;     /* seconds */
    std::string resume_label;   /* "S1:E3" */
    int files = 0, seasons = 0;
};

struct HxRow {
    std::string title;
    std::vector<int> items;     /* into hx_all */
    bool wide = false;          /* landscape cards (Continue Watching) */
};

std::vector<HxTitle> hx_all;
int hx_built = -1;

const char *genre_name(int id)
{
    switch (id) {
    case 28: return "Action";
    case 12: return "Adventure";
    case 16: return "Animation";
    case 35: return "Comedy";
    case 80: return "Crime";
    case 99: return "Documentaries";
    case 18: return "Dramas";
    case 10751: return "Family";
    case 14: return "Fantasy";
    case 36: return "History";
    case 27: return "Horror";
    case 10402: return "Music";
    case 9648: return "Mystery";
    case 10749: return "Romance";
    case 878: return "Sci-Fi";
    case 10770: return "TV Movies";
    case 53: return "Thrillers";
    case 10752: return "War";
    case 37: return "Westerns";
    case 10759: return "Action & Adventure";
    case 10762: return "Kids";
    case 10763: return "News";
    case 10764: return "Reality";
    case 10765: return "Sci-Fi & Fantasy";
    case 10766: return "Soaps";
    case 10767: return "Talk Shows";
    case 10768: return "War & Politics";
    }
    return nullptr;
}

std::string kind_label(const HxTitle &t)
{
    if (t.custom)
        return "Videos";
    if (t.anime)
        return "Anime";
    return t.type == "tv" ? "Series" : "Movie";
}

void hx_rebuild()
{
    int gen = db_generation();
    if (gen == hx_built)
        return;
    hx_built = gen;
    hx_all.clear();
    auto g = db_lock();
    Db &d = db();
    std::map<int, int> files;
    std::map<int, std::set<int>> seasons;
    for (const DbLink &l : d.links)
        if (l.matched && !l.hidden && l.media_id) {
            files[l.media_id]++;
            if (DbEpisode *e = db_episode_by_id(l.episode_id))
                seasons[l.media_id].insert(e->season_id);
        }
    for (const DbMedia &m : d.media) {
        if (!files.count(m.id))
            continue;
        HxTitle t;
        t.id = m.id;
        t.title = m.title;
        t.type = m.type;
        t.poster = m.poster;
        t.backdrop = m.backdrop.empty() ? m.poster : m.backdrop;
        t.overview = m.overview;
        t.year = m.year;
        t.vote = m.vote;
        t.anime = m.anime;
        t.custom = m.type == "custom";
        t.created = m.created;
        t.files = files[m.id];
        t.seasons = (int)seasons[m.id].size();
        size_t at = 0;
        while (at < m.genres.size()) {
            t.genres.push_back(atoi(m.genres.c_str() + at));
            size_t c = m.genres.find(',', at);
            if (c == std::string::npos)
                break;
            at = c + 1;
        }
        auto w = d.watch.find(m.id);
        if (w != d.watch.end() && w->second.watched_at) {
            t.watched_at = w->second.watched_at;
            DbLink *l = db_link(w->second.link_id);
            if (l && l->media_id == m.id && !l->hidden) {
                t.resume_link = l->id;
                t.resume_pos = w->second.position;
                t.progress = w->second.length > 0 ? (float)w->second.position / w->second.length : 0;
                if (DbEpisode *e = db_episode_by_id(l->episode_id)) {
                    DbSeason *s = db_season_by_id(e->season_id);
                    t.resume_label = "S" + std::to_string(s ? s->number : 0) + ":E" + std::to_string(e->number);
                }
            }
        }
        hx_all.push_back(t);
    }
}

const HxTitle *hx_title(int media_id)
{
    for (const HxTitle &t : hx_all)
        if (t.id == media_id)
            return &t;
    return nullptr;
}

/* ---- screens and their state -------------------------------------------------- */

enum HxScreen { HX_ONBOARD, HX_BROWSE, HX_SEARCH, HX_DETAIL, HX_SETTINGS, HX_MANAGE, HX_GROUP, HX_MATCH };
enum HxTab { T_HOME, T_TV, T_MOVIES, T_ANIME, T_CUSTOM, T_MYLIST, T_SEARCH, T_SETTINGS, T_TABS };
const char *const hx_tab_names[T_TABS] = { "Home", "TV Shows", "Movies", "Anime", "Custom", "My List", "Search", "Settings" };

HxScreen hx_screen = HX_ONBOARD;
int sr_focus = -1;       /* search: -1 the keyboard, else a result */
float sr_scroll;
std::vector<HxScreen> hx_back;
int hx_tab = T_HOME;
bool hx_in_nav;          /* the browse screen's focus is on the tab bar */
int hx_nav_focus;
float hx_fade = 1;       /* a screen coming in */

void hx_go(HxScreen s)
{
    hx_back.push_back(hx_screen);
    hx_screen = s;
    hx_fade = 0;
    tick();
}

void hx_go_back()
{
    if (hx_back.empty())
        return;
    hx_screen = hx_back.back();
    hx_back.pop_back();
    hx_fade = 0;
}

/* ---- dialogs over the screens: a menu, typing, the phone page, waiting ---------- */

enum HxDialog { D_NONE, D_MENU, D_PROMPT, D_CATEGORIES, D_PHONE, D_WAIT };
HxDialog hx_dialog = D_NONE;
float hx_dialog_anim;

struct HxMenu {
    std::string title, text;
    std::vector<std::string> items;
    std::function<void(int)> done;
    int focus = 0;
} hx_menu;

struct HxPrompt {
    std::string title, hint, placeholder;
    std::function<void(const std::string &)> done;
} hx_prompt;

std::string hx_wait_text;
std::function<void()> hx_wait_cancel;

void hx_open_menu(const std::string &title, const std::string &text, const std::vector<std::string> &items,
                  std::function<void(int)> done, int focus = 0)
{
    hx_menu = HxMenu{ title, text, items, std::move(done), focus };
    hx_dialog = D_MENU;
    hx_dialog_anim = 0;
    tick();
}

void hx_confirm(const std::string &title, const std::string &text, const std::string &yes, std::function<void()> done)
{
    hx_open_menu(title, text, { yes, "Cancel" }, [done](int i) {
        if (i == 0)
            done();
    }, 1);
}

void hx_open_prompt(const std::string &title, const std::string &hint, const std::string &text, bool link,
                    std::function<void(const std::string &)> done, const std::string &placeholder = "")
{
    kb_open(text, link);
    hx_prompt = HxPrompt{ title, hint, placeholder, std::move(done) };
    hx_dialog = D_PROMPT;
    hx_dialog_anim = 0;
    tick();
}

void hx_menu_draw()
{
    float a = ease(hx_dialog_anim);
    rect(0, 0, 1920, 1080, alpha(NF_BLACK, 0.72f * a));
    int n = (int)hx_menu.items.size();
    float w = 760, text_h = 0;
    if (!hx_menu.text.empty())
        text_h = 34 * (float)std::min(4, 1 + (int)(hx_size(f_reg, 24, hx_menu.text).x / (w - 96))) + 16;
    float h = 128 + text_h + n * 76, x = 960 - w / 2, y = 540 - h / 2 + (1 - a) * 30;
    glow(x, y + 12, w, h, 10, alpha(NF_BLACK, a), 36);
    rect(x, y, w, h, alpha(IM_COL32(24, 24, 24, 252), a), 10);
    hx_text(f_bold, 32, x + 48, y + 40, alpha(NF_WHITE, a), hx_menu.title, 0, w - 96);
    if (!hx_menu.text.empty())
        hx_wrapped(f_reg, 24, x + 48, y + 92, alpha(NF_DIM, a), hx_menu.text, w - 96, 4);
    if (hit(PAD_UP) && n)
        hx_menu.focus = (hx_menu.focus + n - 1) % n;
    if (hit(PAD_DOWN) && n)
        hx_menu.focus = (hx_menu.focus + 1) % n;
    for (int i = 0; i < n; i++) {
        float ry = y + 100 + text_h + i * 76;
        float f = focus_anim(hash_id(hx_menu.items[i], 7100 + (uint64_t)i), i == hx_menu.focus, 18);
        rect(x + 32, ry, w - 64, 64, alpha(mix(IM_COL32(40, 40, 40, 255), NF_WHITE, f), a), 6);
        hx_text(f_semi, 26, x + 64, ry + 16, alpha(mix(NF_WHITE, NF_BLACK, f), a), hx_menu.items[i], 0, w - 128);
    }
    if (hit(PAD_CROSS) && n) {
        auto done = hx_menu.done;
        int i = hx_menu.focus;
        hx_dialog = D_NONE;
        tick();
        if (done)
            done(i);
    } else if (hit(PAD_CIRCLE)) {
        hx_dialog = D_NONE;
    }
}

void hx_prompt_draw()
{
    float a = ease(hx_dialog_anim);
    rect(0, 0, 1920, 1080, alpha(NF_BLACK, 0.85f * a));
    float x = 360, w = 1200, y = 150 + (1 - a) * 30;
    hx_text(f_bold, 44, x, y, alpha(NF_WHITE, a), hx_prompt.title);
    if (!hx_prompt.hint.empty())
        hx_wrapped(f_reg, 24, x, y + 66, alpha(NF_DIM, a), hx_prompt.hint, w, 2);
    text_box(x, y + 150, w, hx_prompt.placeholder.empty() ? "Type here" : hx_prompt.placeholder.c_str(), a, true);
    bool done = kb_frame(x, y + 260, w, true, a, nullptr);
    hints(1824, 1030, { { PAD_SQUARE, "Delete" }, { PAD_TRIANGLE, "Space" }, { PAD_R2, "Done" }, { PAD_CIRCLE, "Cancel" } }, a);
    if (done) {
        auto cb = hx_prompt.done;
        std::string t = kb.text;
        hx_dialog = D_NONE;
        if (cb)
            cb(t);
    } else if (hit(PAD_CIRCLE)) {
        hx_dialog = D_NONE;
    }
}

void hx_phone_draw()
{
    float a = ease(hx_dialog_anim);
    rect(0, 0, 1920, 1080, alpha(NF_BLACK, 0.75f * a));
    float w = 1040, h = 470, x = 960 - w / 2, y = 540 - h / 2 + (1 - a) * 30;
    rect(x, y, w, h, alpha(IM_COL32(24, 24, 24, 252), a), 10);
    text(f_bold, 34, x + 52, y + 48, alpha(NF_WHITE, a), "Use your phone or computer");
    std::string url = web_running() ? web_address() : "";
    if (url.empty()) {
        text(f_reg, 24, x + 52, y + 120, alpha(NF_DIM, a), "The PS5 isn't on a network yet. Connect it, then come back.", 0, w - 104);
    } else {
        text(f_reg, 24, x + 52, y + 112, alpha(NF_DIM, a), "On the same Wi-Fi, open this address in a browser:", 0, w - 104);
        rect(x + 52, y + 164, w - 104, 100, alpha(IM_COL32(40, 40, 40, 255), a), 8);
        hx_text(f_bold, 46, x + w / 2, y + 186, alpha(NF_RED, a), url, 0.5f, w - 140);
        text(f_reg, 23, x + 52, y + 296, alpha(NF_WHITE, a), "Paste your AllDebrid and TMDB keys in the Keys tab.", 0, w - 104);
        text(f_reg, 23, x + 52, y + 336, alpha(NF_WHITE, a), "You can also send subtitle files and add an OpenSubtitles key.", 0, w - 104);
    }
    hints(x + w - 44, y + h - 40, { { PAD_CIRCLE, "Close" } }, a);
    if (hit(PAD_CIRCLE) || hit(PAD_CROSS))
        hx_dialog = D_NONE;
}

void hx_wait_draw()
{
    float a = ease(hx_dialog_anim);
    rect(0, 0, 1920, 1080, alpha(NF_BLACK, 0.7f * a));
    float t = (float)fmod(now * 5.5, 2 * PI_F);
    dl->PathArcTo(P(960, 500), 44 * S, t, t + PI_F * 1.5f, 48);
    dl->PathStroke(alpha(NF_RED, a), 0, 6 * S);
    hx_text(f_semi, 28, 960, 580, alpha(NF_WHITE, a), hx_wait_text, 0.5f, 1400);
    if (hx_wait_cancel)
        hints(1824, 1030, { { PAD_CIRCLE, "Cancel" } }, a);
    if (hx_wait_cancel && hit(PAD_CIRCLE)) {
        auto cancel = hx_wait_cancel;
        hx_dialog = D_NONE;
        cancel();
    }
}

void hx_wait(const std::string &text, std::function<void()> cancel)
{
    hx_wait_text = text;
    hx_wait_cancel = std::move(cancel);
    hx_dialog = D_WAIT;
    hx_dialog_anim = 0;
}

/* ---- playing --------------------------------------------------------------------- */

/* What plays came from the library: its file, so the player's position saves
 * and next / previous go there. */
struct HxNow {
    bool on;
    int link;
    bool cached;    /* the address was one AllDebrid gave earlier */
    bool retried;
} hx_now;

int hx_pending;          /* a file being unlocked to play */
int64_t hx_pending_from; /* ms */
int hx_pending_serial;
bool hx_pending_fresh;

/* Where to start a file: its own saved place (not in its last minutes). */
int64_t hx_resume_ms(int link_id)
{
    auto g = db_lock();
    auto it = db().link_progress.find(link_id);
    if (it == db().link_progress.end())
        return 0;
    int64_t pos = it->second.first, len = it->second.second;
    if (pos < 10 || (len > 0 && pos > len - 120))
        return 0;
    return pos * 1000;
}

void hx_play(int link_id, int64_t from_ms, bool fresh = false)
{
    if (!link_id)
        return;
    if (unlock_status().state == JOB_BUSY) {
        /* an unlock that was cancelled is still waiting for AllDebrid */
        toast("Still talking to AllDebrid: try again in a moment", 2.5);
        return;
    }
    hx_pending = link_id;
    hx_pending_from = from_ms;
    hx_pending_fresh = fresh;
    unlock_start(link_id, fresh);
    hx_pending_serial = unlock_status().serial;
    hx_wait(fresh ? "Getting a new stream from AllDebrid\xE2\x80\xA6" : "Getting the stream from AllDebrid\xE2\x80\xA6",
            [] { hx_pending = 0; });
}

/* every frame: an unlock that finished starts the player */
void hx_pending_tick()
{
    if (!hx_pending)
        return;
    JobStatus s = unlock_status();
    if (s.serial != hx_pending_serial || s.state == JOB_BUSY)
        return;
    int link = hx_pending;
    hx_pending = 0;
    if (hx_dialog == D_WAIT)
        hx_dialog = D_NONE;
    if (s.state == JOB_FAILED || unlock_link_id() != link) {
        toast(s.message.empty() ? "Couldn't get the stream" : s.message, 5);
        return;
    }
    bool retried = hx_pending_fresh;
    hx_now = { true, link, unlock_was_cached(), retried };
    start_stream(unlock_url(), {}, false, link_title(link), nullptr, hx_pending_from);
    if (screen != PLAYER)
        hx_now.on = false; /* it didn't open */
}

bool hix_playing()
{
    return hx_now.on;
}

/* the player saves where it is (save_position) */
bool hix_save(bool finished)
{
    if (!hx_now.on)
        return false;
    int64_t t = finished ? 0 : player_time(), len = player_length();
    if (!finished && len <= 0)
        return true;
    save_progress(hx_now.link, t, len, finished);
    return true;
}

/* back to the library with a file to unlock (next episode, a retry) */
void hx_switch_to(int link, int64_t from_ms, bool fresh)
{
    player_stop();
    video_forget_picture();
    library_set_busy(false);
    screen = LIBRARY;
    screen_fade = 1;
    modal = NONE;
    playing_path.clear();
    hx_now.on = false;
    hx_play(link, from_ms, fresh);
}

/* L1 / R1 in the player, or its Previous / Next buttons */
bool hix_neighbour(int step)
{
    if (!hx_now.on)
        return false;
    int n = neighbour_link(hx_now.link, step);
    if (!n) {
        toast(step > 0 ? "That was the last one" : "That's the first one");
        return true;
    }
    save_position(false);
    hx_switch_to(n, hx_resume_ms(n), false);
    return true;
}

/* the end of a file: the next episode plays by itself */
bool hix_ended()
{
    if (!hx_now.on)
        return false;
    save_position(true);
    int n = neighbour_link(hx_now.link, 1);
    if (!n)
        return false;
    toast("Next: " + link_title(n), 4);
    hx_switch_to(n, 0, false);
    return true;
}

/* VLC couldn't open it: a direct link from earlier may have expired, so once
 * more with a new one */
bool hix_failed()
{
    if (!hx_now.on || !hx_now.cached || hx_now.retried)
        return false;
    int link = hx_now.link;
    int64_t from = hx_resume_ms(link);
    hx_switch_to(link, from, true);
    return true;
}

void hix_left_player()
{
    hx_now.on = false;
}

/* ---- the first page ------------------------------------------------------------ */

int ob_step;          /* 0 AllDebrid, 1 TMDB */
int ob_serial = -1;   /* the key check we wait for */
std::string ob_message;
bool ob_message_bad;

void hx_finish_onboarding()
{
    hx_screen = HX_BROWSE;
    hx_back.clear();
    hx_tab = T_HOME;
    hx_in_nav = false;
    hx_fade = 0;
    bool empty;
    {
        auto g = db_lock();
        empty = db().links.empty();
    }
    if (empty)
        scan_start();
}

void hx_onboard()
{
    /* a black stage with a red light behind the logo */
    rect(0, 0, 1920, 1080, NF_BLACK);
    radial(960, -120, 1100, IM_COL32(229, 9, 20, 46));
    hx_logo(960, 70, 96, 1, 0.5f);

    JobStatus ks = keys_check_status();
    bool checking = ks.state == JOB_BUSY && ks.serial == ob_serial;
    if (ks.serial == ob_serial && ks.state != JOB_BUSY && ob_serial >= 0) {
        ob_serial = -1;
        ob_message = ks.message;
        ob_message_bad = ks.state == JOB_FAILED;
        if (!ob_message_bad) {
            bool need_tmdb = tmdb_key().empty();
            if (ob_step == 0 && need_tmdb) {
                ob_step = 1;
                kb_open("", false);
            } else {
                toast(ob_message, 4);
                hx_finish_onboarding();
                return;
            }
        }
    }

    float x = 460, w = 1000;
    const char *title = ob_step == 0 ? "Welcome to HixFlix" : "One more thing: TMDB";
    text(f_bold, 54, 960, 220, NF_WHITE, title, 0.5f);
    std::string sub = ob_step == 0
        ? "Enter your AllDebrid API key to watch your AllDebrid library. It stays on this PS5."
        : "A free TMDB key brings the posters, titles, seasons and episodes. Recommended.";
    hx_wrapped(f_reg, 26, x, 300, NF_DIM, tr(sub.c_str()), w, 2);
    std::string where = ob_step == 0 ? "alldebrid.com/apikeys" : "themoviedb.org/settings/api";
    text(f_reg, 22, x, 350, NF_FAINT, trf("Get one at %s", where.c_str()).c_str());

    text_box(x, 400, w, ob_step == 0 ? "AllDebrid API key" : "TMDB API key or read access token", 1, !checking);
    if (checking) {
        float t = (float)fmod(now * 5.5, 2 * PI_F);
        dl->PathArcTo(P(960, 700), 40 * S, t, t + PI_F * 1.5f, 48);
        dl->PathStroke(NF_RED, 0, 6 * S);
        hx_text(f_semi, 26, 960, 770, NF_WHITE, ks.message, 0.5f);
    } else {
        if (!ob_message.empty())
            hx_text(f_semi, 22, x, 486, ob_message_bad ? IM_COL32(255, 110, 110, 255) : NF_GREEN, ob_message, 0, w);
        bool done = hx_dialog == D_NONE && kb_frame(x, 530, w, hx_dialog == D_NONE, 1, nullptr);
        if (done) {
            std::string key = kb.text;
            while (!key.empty() && key.back() == ' ')
                key.pop_back();
            if (key.empty()) {
                ob_message = ob_step == 0 ? "Type or paste your AllDebrid API key first" : "Type a TMDB key, or press \xE2\x96\xB3 to skip";
                ob_message_bad = true;
            } else if (ob_step == 0) {
                keys_check_start(key, "");
                ob_serial = keys_check_status().serial;
                ob_message.clear();
            } else {
                std::string ad;
                {
                    auto g = db_lock();
                    ad = db().alldebrid_key;
                }
                keys_check_start(ad, key);
                ob_serial = keys_check_status().serial;
                ob_message.clear();
            }
        }
    }
    /* the phone page: keys pasted there arrive through hx_web_frame */
    std::string url = web_running() ? web_address() : "";
    if (!url.empty()) {
        rect(x, 940, w, 64, IM_COL32(32, 32, 32, 255), 8);
        hx_text(f_reg, 22, x + 24, 959, NF_DIM, tr("Easier: open this on your phone or computer and paste it there"), 0, 640);
        hx_text(f_bold, 26, x + w - 24, 956, NF_RED, url, 1);
    } else {
        /* no page (the sandbox, or no network): the key file instead */
        rect(x, 904, w, 112, IM_COL32(32, 32, 32, 255), 8);
        hx_text(f_reg, 21, x + 24, 914, NF_DIM, tr("Or with a file manager: save your key in a file named"), 0, w - 48);
        hx_text(f_bold, 22, x + 24, 944, NF_WHITE,
                std::string(HX_KEY_FILE "   in   /data/homebrew/") + VLC_PS5_TITLE_ID + "/", 0, w - 48);
        hx_text(f_reg, 18, x + 24, 980, NF_FAINT,
                tr(web_running() ? "HixFlix picks it up by itself. (The phone page waits for a network.)"
                                 : "HixFlix picks it up by itself. (The phone page needs etaHEN or the Helper running.)"), 0, w - 48);
    }
    if (hx_dialog == D_NONE && !checking) {
        if (ob_step == 1)
            hints(1824, 1050, { { PAD_TRIANGLE, "Skip" }, { PAD_R2, "Continue" } });
        else
            hints(1824, 1050, { { PAD_SQUARE, "Delete" }, { PAD_R2, "Continue" } });
        if (ob_step == 1 && hit(PAD_TRIANGLE) && kb.text.empty()) {
            hx_finish_onboarding();
            toast("You can add a TMDB key later in Settings", 4);
        }
    }
}

/* ---- the browse screen: billboard and rows ----------------------------------- */

struct HxBrowse {
    int row = -1;                     /* -1: the billboard's buttons */
    int button;                       /* 0 Play, 1 More Info */
    std::map<std::string, int> col;   /* by row title */
    std::map<std::string, float> scroll;
    float row_scroll = 0;
    int featured = 0;                 /* media id */
} hx_browse[T_MYLIST + 1];

std::vector<HxRow> hx_rows;
int hx_rows_for = -1, hx_rows_gen = -1;

std::vector<int> sorted_by(std::vector<int> items, bool recent_watch)
{
    std::sort(items.begin(), items.end(), [&](int a, int b) {
        const HxTitle &x = hx_all[a], &y = hx_all[b];
        if (recent_watch)
            return x.watched_at > y.watched_at;
        return x.created != y.created ? x.created > y.created : x.id > y.id;
    });
    return items;
}

void hx_build_rows()
{
    if (hx_rows_for == hx_tab && hx_rows_gen == hx_built)
        return;
    hx_rows_for = hx_tab;
    hx_rows_gen = hx_built;
    hx_rows.clear();
    auto of = [&](std::function<bool(const HxTitle &)> keep) {
        std::vector<int> v;
        for (size_t i = 0; i < hx_all.size(); i++)
            if (keep(hx_all[i]))
                v.push_back((int)i);
        return v;
    };
    auto add = [&](const std::string &title, std::vector<int> items, bool wide = false) {
        if (!items.empty())
            hx_rows.push_back({ title, std::move(items), wide });
    };
    auto by_genre = [&](const std::vector<int> &pool) {
        std::map<int, std::vector<int>> g;
        for (int i : pool)
            for (int id : hx_all[i].genres)
                if (genre_name(id))
                    g[id].push_back(i);
        std::vector<std::pair<int, int>> order; /* (count, genre) */
        for (auto &kv : g)
            if (kv.second.size() >= 2 || pool.size() < 12)
                order.push_back({ (int)kv.second.size(), kv.first });
        std::sort(order.rbegin(), order.rend());
        for (auto &o : order)
            add(genre_name(o.second), sorted_by(g[o.second], false));
    };
    auto watching = [](const HxTitle &t) { return t.watched_at > 0 && t.resume_link; };
    std::vector<int> pool;
    switch (hx_tab) {
    case T_HOME:
        add("Continue Watching", sorted_by(of(watching), true), true);
        add("Recently Added", sorted_by(of([](const HxTitle &) { return true; }), false));
        add("TV Shows", sorted_by(of([](const HxTitle &t) { return t.type == "tv" && !t.anime; }), false));
        add("Movies", sorted_by(of([](const HxTitle &t) { return t.type == "movie" && !t.anime; }), false));
        add("Anime", sorted_by(of([](const HxTitle &t) { return t.anime; }), false));
        add("Custom Videos", sorted_by(of([](const HxTitle &t) { return t.custom; }), false));
        {
            auto g = db_lock();
            for (const DbCategory &c : db().categories) {
                std::set<int> ids;
                for (auto &mc : db().media_categories)
                    if (mc.second == c.id)
                        ids.insert(mc.first);
                add(c.name, sorted_by(of([&](const HxTitle &t) { return ids.count(t.id) > 0; }), false));
            }
        }
        break;
    case T_TV:
    case T_MOVIES:
    case T_ANIME:
    case T_CUSTOM: {
        int tab = hx_tab;
        auto in_tab = [tab](const HxTitle &t) {
            return tab == T_TV ? t.type == "tv" && !t.anime : tab == T_MOVIES ? t.type == "movie" && !t.anime
                 : tab == T_ANIME ? t.anime : t.custom || (t.type == "tv" && t.genres.empty() && !t.anime);
        };
        pool = of(in_tab);
        add("Continue Watching", sorted_by(of([&](const HxTitle &t) { return in_tab(t) && watching(t); }), true), true);
        add(tab == T_TV ? "All TV Shows" : tab == T_MOVIES ? "All Movies" : tab == T_ANIME ? "All Anime" : "All Custom",
            sorted_by(pool, false));
        if (tab != T_CUSTOM)
            by_genre(pool);
        break;
    }
    case T_MYLIST: {
        auto g = db_lock();
        for (const DbCategory &c : db().categories) {
            std::set<int> ids;
            for (auto &mc : db().media_categories)
                if (mc.second == c.id)
                    ids.insert(mc.first);
            add(c.name, sorted_by(of([&](const HxTitle &t) { return ids.count(t.id) > 0; }), false));
        }
        break;
    }
    }
    /* the billboard: one of the newest titles with a picture */
    HxBrowse &b = hx_browse[hx_tab];
    if (!hx_title(b.featured) || hx_rows.empty()) {
        std::vector<int> pick;
        for (const HxRow &r : hx_rows)
            for (int i : r.items)
                if (!hx_all[i].backdrop.empty() && pick.size() < 8)
                    pick.push_back(hx_all[i].id);
        b.featured = pick.empty() ? (hx_rows.empty() ? 0 : hx_all[hx_rows[0].items[0]].id)
                                  : pick[(size_t)rand() % pick.size()];
    }
    if (b.row >= (int)hx_rows.size())
        b.row = (int)hx_rows.size() - 1;
}

void hx_open_detail(int media_id);

/* the backdrop of the title in focus, crossfading when it changes */
void hx_billboard_backdrop(const HxTitle *t)
{
    static std::string shown, before;
    static float mixk = 1;
    std::string url = t ? db_image_url(t->backdrop, "w1280") : "";
    if (url != shown) {
        before = shown;
        shown = url;
        mixk = 0;
    }
    mixk = approach(mixk, 1, 6);
    rect(0, 0, 1920, 1080, NF_BG);
    float bx = 560, bw = 1360, bh = bw * 9 / 16;
    if (!before.empty() && mixk < 0.99f)
        hx_cover(before, bx, 0, bw, bh, 1 - mixk);
    if (!shown.empty())
        hx_cover(shown, bx, 0, bw, bh, hix_image(shown) ? mixk : 0);
    /* dark on the left for the words, and into the page at the bottom */
    hgrad(bx, 0, 640, bh, NF_BG, alpha(NF_BG, 0));
    dgrad(bx, bh - 360, bw, 360, alpha(NF_BG, 0), NF_BG);
    dgrad(0, 0, 1920, 180, alpha(NF_BLACK, 0.75f), alpha(NF_BLACK, 0));
}

/* the top bar: logo and tabs; returns the bar's height */
void hx_nav_bar(bool focused)
{
    hx_logo(72, 34, 50);
    float x = 340;
    for (int i = 0; i < T_TABS; i++) {
        bool icon = i == T_SEARCH || i == T_SETTINGS;
        float w = icon ? 56 : text_size(f_semi, 25, hx_tab_names[i]).x + 36;
        if (i == T_SEARCH)
            x = 1920 - 72 - 56 * 2 - 20;
        bool foc = focused && hx_nav_focus == i, cur = hx_tab == i;
        float f = focus_anim(hash_id(hx_tab_names[i], 7200), foc, 18);
        if (f > 0.01f)
            rect(x, 34, w, 52, alpha(NF_WHITE, f), 26);
        ImU32 ink = mix(cur ? NF_WHITE : NF_DIM, NF_BLACK, f);
        if (i == T_SEARCH)
            icon_search(x + w / 2, 60, 30, ink);
        else if (i == T_SETTINGS)
            icon_gear(x + w / 2, 60, 30, ink);
        else
            text(cur ? f_bold : f_semi, 25, x + 18, 45, ink, hx_tab_names[i]);
        if (cur && !icon && f < 0.5f)
            rect(x + 18, 84, w - 36, 3, NF_RED, 1);
        x += w + 8;
    }
}

void hx_select_tab(int i)
{
    if (i == T_SEARCH) {
        kb_open("", false);
        sr_focus = -1;
        hx_go(HX_SEARCH);
        return;
    }
    if (i == T_SETTINGS) {
        hx_go(HX_SETTINGS);
        return;
    }
    hx_tab = i;
    hx_fade = 0;
}

/* a poster card; f: focus 0..1 */
void hx_poster_card(const HxTitle &t, float x, float y, float w, float h, float f, float a)
{
    float s = 1 + 0.1f * f, cw = w * s, ch = h * s, cx = x - (cw - w) / 2, cy = y - (ch - h) / 2;
    if (f > 0.01f)
        glow(cx, cy, cw, ch, 6, alpha(NF_BLACK, a * f), 26);
    std::string url = db_image_url(t.poster, "w342");
    if (url.empty() || !hx_cover(url, cx, cy, cw, ch, a, 6)) {
        dl->AddRectFilledMultiColor(P(cx, cy), P(cx + cw, cy + ch), alpha(tint_for(t.title, 0.35f), a),
                                    alpha(tint_for(t.title + "#", 0.25f), a), alpha(NF_CARD, a), alpha(NF_CARD, a));
        hx_wrapped(f_bold, 24, cx + 16, cy + ch - 120, alpha(NF_WHITE, a), t.title, cw - 32, 3, 1.25f);
    }
    if (t.progress > 0.01f)
        hx_progress(cx + 10, cy + ch - 14, cw - 20, t.progress, a);
    if (f > 0.01f)
        stroke(cx - 2, cy - 2, cw + 4, ch + 4, alpha(NF_WHITE, f * a), 8, 4);
}

/* a landscape card: the backdrop, a progress line, the title under it */
void hx_wide_card(const HxTitle &t, float x, float y, float w, float h, float f, float a)
{
    float s = 1 + 0.08f * f, cw = w * s, ch = h * s, cx = x - (cw - w) / 2, cy = y - (ch - h) / 2;
    if (f > 0.01f)
        glow(cx, cy, cw, ch, 6, alpha(NF_BLACK, a * f), 26);
    std::string url = db_image_url(t.backdrop, "w780");
    if (url.empty() || !hx_cover(url, cx, cy, cw, ch, a, 6))
        rect(cx, cy, cw, ch, alpha(tint_for(t.title, 0.3f), a), 6);
    dgrad(cx, cy + ch * 0.45f, cw, ch * 0.55f, alpha(NF_BLACK, 0), alpha(NF_BLACK, 0.85f * a));
    hx_text(f_bold, 24, cx + 16, cy + ch - 62, alpha(NF_WHITE, a), t.title, 0, cw - 32);
    if (!t.resume_label.empty())
        hx_text(f_semi, 19, cx + 16, cy + ch - 34, alpha(NF_DIM, a), t.resume_label, 0, cw - 32);
    hx_progress(cx + 16, cy + ch - 10, cw - 32, t.progress, a);
    if (f > 0.01f)
        stroke(cx - 2, cy - 2, cw + 4, ch + 4, alpha(NF_WHITE, f * a), 8, 4);
}

/* the words of a title: name, meta line, overview */
void hx_title_info(const HxTitle &t, float x, float y, float w, float a, int lines)
{
    float ts = t.title.size() > 28 ? 58 : 76;
    int n = hx_wrapped(f_bold, ts, x, y, alpha(NF_WHITE, a), t.title, w, 2, 1.1f);
    float my = y + n * ts * 1.1f + 18, mx = x;
    if (t.vote > 0) {
        char v[16];
        snprintf(v, sizeof v, "\xE2\x98\x85 %.1f", t.vote);
        hx_text(f_bold, 26, mx, my, alpha(NF_GREEN, a), v);
        mx += hx_size(f_bold, 26, v).x + 24;
    }
    if (t.year) {
        hx_text(f_semi, 26, mx, my, alpha(NF_DIM, a), std::to_string(t.year));
        mx += hx_size(f_semi, 26, std::to_string(t.year)).x + 24;
    }
    std::string kind = tr(kind_label(t).c_str());
    stroke(mx, my + 1, hx_size(f_semi, 20, kind).x + 20, 32, alpha(NF_DIM, 0.8f * a), 4, 1.5f);
    hx_text(f_semi, 20, mx + 10, my + 5, alpha(NF_DIM, a), kind);
    mx += hx_size(f_semi, 20, kind).x + 44;
    std::string count;
    if (t.type == "tv")
        count = t.seasons > 1 ? trf("%d Seasons", t.seasons) : count_of(t.files, "%d episode", "%d episodes");
    else if (t.files > 1)
        count = count_of(t.files, "%d file", "%d files");
    if (!count.empty())
        hx_text(f_semi, 26, mx, my, alpha(NF_DIM, a), count);
    if (lines > 0 && !t.overview.empty())
        hx_wrapped(f_reg, 26, x, my + 60, alpha(IM_COL32(230, 230, 230, 255), a), t.overview, w, lines);
}

void hx_browse_screen()
{
    hx_build_rows();
    HxBrowse &b = hx_browse[hx_tab];
    int nrows = (int)hx_rows.size();
    if (b.row >= nrows)
        b.row = nrows - 1;
    if (nrows == 0)
        b.row = -1;
    bool input = hx_dialog == D_NONE;
    bool was_nav = hx_in_nav; /* ✕ on a tab mustn't also press what's under the bar */

    /* what's in focus */
    const HxTitle *focus = hx_title(b.featured);
    if (b.row >= 0) {
        const HxRow &r = hx_rows[b.row];
        int c = std::min(b.col[r.title], (int)r.items.size() - 1);
        focus = &hx_all[r.items[std::max(0, c)]];
    }
    hx_billboard_backdrop(focus);

    if (input) {
        if (hx_in_nav) {
            if (hit(PAD_LEFT))
                hx_nav_focus = (hx_nav_focus + T_TABS - 1) % T_TABS;
            if (hit(PAD_RIGHT))
                hx_nav_focus = (hx_nav_focus + 1) % T_TABS;
            if (hit(PAD_CROSS)) {
                hx_select_tab(hx_nav_focus);
                hx_in_nav = hx_nav_focus >= T_SEARCH;
            } else if (hit(PAD_DOWN) || hit(PAD_CIRCLE)) {
                hx_in_nav = false;
            }
        } else {
            if (hit(PAD_L1) || hit(PAD_R1)) {
                hx_tab = (hx_tab + (hit(PAD_R1) ? 1 : T_MYLIST)) % (T_MYLIST + 1);
                hx_nav_focus = hx_tab;
                hx_fade = 0;
                tick();
                return;
            }
            if (hit(PAD_UP)) {
                if (b.row > -1 && !(b.row == 0 && !focus))
                    b.row--;
                else {
                    hx_in_nav = true;
                    hx_nav_focus = hx_tab;
                }
                if (b.row == -1 && !hx_title(b.featured)) {
                    hx_in_nav = true;
                    b.row = 0;
                }
            } else if (hit(PAD_DOWN) && b.row + 1 < nrows) {
                b.row++;
            } else if (hit(PAD_CIRCLE)) {
                hx_in_nav = true;
                hx_nav_focus = hx_tab;
            }
            if (b.row == -1) {
                if (hit(PAD_LEFT) || hit(PAD_RIGHT))
                    b.button ^= 1;
                if (hit(PAD_CROSS) && focus) {
                    if (b.button == 0) {
                        const HxTitle *t = focus;
                        int link = t->resume_link;
                        int64_t from = t->resume_pos * 1000;
                        if (!link) {
                            link = 0;
                            auto g = db_lock();
                            for (const DbLink &l : db().links)
                                if (l.media_id == t->id && l.matched && !l.hidden) {
                                    link = l.id;
                                    break;
                                }
                            if (link) {
                                /* the first episode, in play order */
                                int prev;
                                while ((prev = neighbour_link(link, -1)))
                                    link = prev;
                            }
                            from = 0;
                        }
                        hx_play(link, from);
                    } else {
                        hx_open_detail(focus->id);
                    }
                }
            } else if (b.row >= 0) {
                const HxRow &r = hx_rows[b.row];
                int &c = b.col[r.title];
                int n = (int)r.items.size();
                if (hit(PAD_LEFT) && c > 0)
                    c--;
                if (hit(PAD_RIGHT) && c + 1 < n)
                    c++;
                if (hit(PAD_L2))
                    c = std::max(0, c - 8);
                if (hit(PAD_R2))
                    c = std::min(n - 1, c + 8);
                if (hit(PAD_CROSS))
                    hx_open_detail(hx_all[r.items[c]].id);
                if (hit(PAD_TRIANGLE)) {
                    const HxTitle &t = hx_all[r.items[c]];
                    if (t.resume_link)
                        hx_play(t.resume_link, t.resume_pos * 1000);
                    else
                        hx_open_detail(t.id);
                }
            }
        }
    }

    float fa = ease(hx_fade);
    /* the billboard's words */
    if (focus) {
        float iy = b.row == -1 ? 190 : 170;
        hx_title_info(*focus, 96, iy, 860, fa, b.row == -1 ? 3 : 2);
        if (b.row == -1) {
            bool resume = focus->resume_link && focus->resume_pos > 0;
            float f0 = focus_anim(7300, !hx_in_nav && b.button == 0, 16);
            float f1 = focus_anim(7301, !hx_in_nav && b.button == 1, 16);
            hx_button(96, 560, 230, 72, resume ? "Resume" : "Play", f0, fa, 1);
            hx_button(346, 560, 300, 72, "More Info", f1, fa, 2);
        }
    } else if (nrows == 0) {
        /* nothing to show: the scan, or how to fill the library */
        JobStatus st = scan_status();
        float cy = 430;
        if (st.state == JOB_BUSY) {
            float t = (float)fmod(now * 5.5, 2 * PI_F);
            dl->PathArcTo(P(960, cy), 44 * S, t, t + PI_F * 1.5f, 48);
            dl->PathStroke(NF_RED, 0, 6 * S);
            text(f_bold, 40, 960, cy + 80, NF_WHITE, "Building your library", 0.5f);
            hx_text(f_reg, 26, 960, cy + 140, NF_DIM, st.message, 0.5f, 1400);
            if (st.progress >= 0) {
                rect(660, cy + 196, 600, 6, IM_COL32(70, 70, 70, 255), 3);
                rect(660, cy + 196, 600 * st.progress, 6, NF_RED, 3);
            }
        } else if (hx_tab == T_MYLIST) {
            text(f_bold, 40, 960, cy, NF_WHITE, "My List is empty", 0.5f);
            text(f_reg, 26, 960, cy + 64, NF_DIM, "On a title's page, choose My List to put it in a list of your own.", 0.5f);
        } else if (hx_tab != T_HOME) {
            text(f_bold, 40, 960, cy, NF_WHITE, trf("No %s yet", tr(hx_tab_names[hx_tab])).c_str(), 0.5f);
        } else {
            text(f_bold, 44, 960, cy, NF_WHITE, "Your library is empty", 0.5f);
            if (!st.message.empty())
                hx_text(f_reg, 26, 960, cy + 70, st.state == JOB_FAILED ? IM_COL32(255, 110, 110, 255) : NF_DIM, st.message, 0.5f, 1500);
            text(f_reg, 26, 960, cy + 120, NF_DIM, "Scan your AllDebrid magnets, or match the files in Manage library.", 0.5f);
            float f = focus_anim(7302, !hx_in_nav, 16);
            hx_button(960 - 170, cy + 200, 340, 72, "Scan AllDebrid", f, 1, 3);
            if (input && !was_nav && hit(PAD_CROSS))
                scan_start();
        }
    }

    /* the rows: the one in focus at a fixed height, the others around it */
    float base = 640; /* the row in focus; the next one peeks in under it */
    float target = (float)std::max(0, b.row) * 1.0f;
    b.row_scroll = approach(b.row_scroll, target, 10);
    for (int ri = 0; ri < nrows; ri++) {
        const HxRow &r = hx_rows[ri];
        float rel = ri - b.row_scroll;
        float card_h = r.wide ? 200 : 300, card_w = r.wide ? 356 : 200, gap = r.wide ? 20 : 16;
        float y = base + rel * 370;
        if (b.row == -1)
            y = 760 + ri * 370;
        if (y < 120 || y > 1080)
            continue;
        float ra = fa * (rel < -0.3f ? std::max(0.0f, 1 + (rel + 0.3f) * 2) : 1);
        bool row_focus = !hx_in_nav && ri == b.row;
        hx_text(f_bold, 30, 96, y - 52, alpha(NF_WHITE, ra), r.title);
        int c = b.col[r.title];
        float &sc = b.scroll[r.title];
        sc = approach(sc, (float)c, 12);
        float pitch = card_w + gap;
        for (int i = std::max(0, (int)sc - 2); i < (int)r.items.size(); i++) {
            float x = 96 + (i - sc) * pitch;
            if (x > 1920)
                break;
            if (x + card_w < -40)
                continue;
            const HxTitle &t = hx_all[r.items[i]];
            float f = focus_anim(hash_id(r.title, 7400 + (uint64_t)t.id), row_focus && i == c, 16);
            float ia = ra * (x < 60 && !(row_focus && i == c) ? std::max(0.0f, (x + card_w) / (card_w + 60)) : 1);
            if (r.wide)
                hx_wide_card(t, x, y, card_w, card_h, f, ia);
            else
                hx_poster_card(t, x, y, card_w, card_h, f, ia);
        }
    }
    /* the row peeking in at the bottom stays under a dark band, with the hints on it */
    dgrad(0, 960, 1920, 50, alpha(NF_BG, 0), NF_BG);
    rect(0, 1010, 1920, 70, NF_BG);
    hx_nav_bar(hx_in_nav && input);
    if (input) {
        if (hx_in_nav)
            hints(1824, 1050, { { PAD_CROSS, "Open" }, { PAD_DOWN, "Browse" } });
        else if (b.row == -1)
            hints(1824, 1050, { { PAD_CROSS, "Select" }, { PAD_R1, "Tabs" }, { PAD_OPTIONS, "Settings" } });
        else if (nrows)
            hints(1824, 1050, { { PAD_CROSS, "Details" }, { PAD_TRIANGLE, "Play" }, { PAD_R1, "Tabs" },
                                { PAD_R3, "Search" }, { PAD_OPTIONS, "Settings" } });
    }
}

/* ---- search -------------------------------------------------------------------- */


std::string lower_text(std::string s)
{
    for (char &c : s)
        c = (char)tolower((unsigned char)c);
    return s;
}

void hx_search_screen()
{
    rect(0, 0, 1920, 1080, NF_BG);
    bool input = hx_dialog == D_NONE;
    std::string q = lower_text(kb.text);
    static std::vector<int> found;
    static std::string found_for = "\x01";
    static int found_gen = -1;
    if (q != found_for || found_gen != hx_built) {
        found_for = q;
        found_gen = hx_built;
        found.clear();
        std::vector<std::string> names(hx_all.size());
        for (size_t i = 0; i < hx_all.size(); i++) {
            names[i] = lower_text(hx_all[i].title);
            if (q.empty() || names[i].find(q) != std::string::npos)
                found.push_back((int)i);
        }
        std::sort(found.begin(), found.end(), [&](int a, int b) { return names[a] < names[b]; });
    }
    if (sr_focus >= (int)found.size())
        sr_focus = (int)found.size() - 1;

    hx_logo(72, 34, 50);
    text(f_bold, 40, 72, 120, NF_WHITE, "Search");
    text_box(72, 190, 660, "Titles", 1, sr_focus < 0);
    bool done = kb_frame(72, 300, 660, input && sr_focus < 0, 1, nullptr);
    if (input && sr_focus < 0) {
        if ((done || hit(PAD_R1)) && !found.empty())
            sr_focus = 0;
        if (hit(PAD_CIRCLE)) {
            hx_go_back();
            return;
        }
    }
    const int cols = 5;
    float gx = 800, gw = 1060, cw = 188, ch = 282, gap = (gw - cols * cw) / (cols - 1), rh = ch + 70;
    if (input && sr_focus >= 0) {
        int n = (int)found.size();
        if (hit(PAD_LEFT)) {
            if (sr_focus % cols == 0)
                sr_focus = -1;
            else
                sr_focus--;
        } else if (hit(PAD_RIGHT) && sr_focus + 1 < n && (sr_focus + 1) % cols) {
            sr_focus++;
        } else if (hit(PAD_UP) && sr_focus >= cols) {
            sr_focus -= cols;
        } else if (hit(PAD_DOWN) && sr_focus + cols < n) {
            sr_focus += cols;
        } else if (hit(PAD_CROSS)) {
            hx_open_detail(hx_all[found[sr_focus]].id);
            return;
        } else if (hit(PAD_CIRCLE)) {
            sr_focus = -1;
        }
    }
    text(f_semi, 26, gx, 120, NF_DIM, q.empty() ? "Your library" : count_of((int)found.size(), "%d result", "%d results").c_str());
    int frow = std::max(0, sr_focus) / cols;
    sr_scroll = approach(sr_scroll, (float)std::max(0, frow - 1), 10);
    for (size_t i = 0; i < found.size(); i++) {
        int r = (int)i / cols, c = (int)i % cols;
        float y = 190 + (r - sr_scroll) * rh, x = gx + c * (cw + gap);
        if (y + ch < 100 || y > 1080)
            continue;
        const HxTitle &t = hx_all[found[i]];
        float f = focus_anim(hash_id(t.title, 7500 + (uint64_t)t.id), sr_focus == (int)i, 16);
        hx_poster_card(t, x, y, cw, ch, f, 1);
        if (f > 0.3f)
            hx_text(f_semi, 20, x + cw / 2, y + ch + 22, alpha(NF_WHITE, f), t.title, 0.5f, cw + 20);
    }
    if (found.empty() && !q.empty())
        text(f_reg, 26, gx, 200, NF_DIM, "Nothing in your library matches that.");
    if (input)
        hints(1824, 1050, sr_focus < 0 ? std::vector<std::pair<uint32_t, const char *>>{ { PAD_R2, "Results" }, { PAD_SQUARE, "Delete" }, { PAD_CIRCLE, "Back" } }
                                       : std::vector<std::pair<uint32_t, const char *>>{ { PAD_CROSS, "Details" }, { PAD_CIRCLE, "Keyboard" } });
}

/* ---- a title's page ------------------------------------------------------------- */

struct HxEpisodeRow {
    int link_id;
    int number;
    std::string name, overview, still;
    int runtime;
    float progress;
};

int dt_media;
int dt_zone;                    /* 0 buttons, 1 seasons, 2 episodes */
int dt_button;
int dt_season_i;                /* into dt_seasons */
int dt_episode;
float dt_ep_scroll;
std::vector<std::pair<int, std::string>> dt_seasons; /* (season number, name) */
std::vector<HxEpisodeRow> dt_rows;
int dt_built = -1, dt_built_season = -2;
bool dt_fresh;                  /* just opened: focus the episode to resume */

void hx_open_detail(int media_id)
{
    dt_media = media_id;
    dt_zone = 0;
    dt_button = 0;
    dt_episode = 0;
    dt_ep_scroll = 0;
    dt_built = -1;
    dt_season_i = -1; /* chosen when the rows are built: the season to resume */
    dt_fresh = true;
    hx_go(HX_DETAIL);
}

void hx_build_detail()
{
    if (dt_built == hx_built && dt_built_season == dt_season_i)
        return;
    auto g = db_lock();
    const HxTitle *t = hx_title(dt_media);
    dt_seasons.clear();
    std::map<int, std::string> seasons;
    std::vector<const DbLink *> loose; /* files that aren't episodes */
    for (const DbLink &l : db().links) {
        if (l.media_id != dt_media || !l.matched || l.hidden)
            continue;
        DbEpisode *e = db_episode_by_id(l.episode_id);
        DbSeason *s = e ? db_season_by_id(e->season_id) : nullptr;
        if (s)
            seasons[s->number] = s->name.empty() ? "Season " + std::to_string(s->number) : s->name;
        else
            loose.push_back(&l);
    }
    for (auto &kv : seasons)
        dt_seasons.push_back(kv);
    if (!loose.empty())
        dt_seasons.push_back({ -1, seasons.empty() ? "Files" : "Other files" });
    if (dt_season_i < 0 || dt_season_i >= (int)dt_seasons.size()) {
        dt_season_i = 0;
        /* the season of the file to resume */
        if (t && t->resume_link)
            if (DbLink *l = db_link(t->resume_link))
                if (DbEpisode *e = db_episode_by_id(l->episode_id))
                    if (DbSeason *s = db_season_by_id(e->season_id))
                        for (size_t i = 0; i < dt_seasons.size(); i++)
                            if (dt_seasons[i].first == s->number)
                                dt_season_i = (int)i;
    }
    dt_built = hx_built;
    dt_built_season = dt_season_i;
    dt_rows.clear();
    if (dt_seasons.empty())
        return;
    int want = dt_seasons[dt_season_i].first;
    for (const DbLink &l : db().links) {
        if (l.media_id != dt_media || !l.matched || l.hidden)
            continue;
        DbEpisode *e = db_episode_by_id(l.episode_id);
        DbSeason *s = e ? db_season_by_id(e->season_id) : nullptr;
        if ((s ? s->number : -1) != want)
            continue;
        HxEpisodeRow r;
        r.link_id = l.id;
        r.number = e ? e->number : 0;
        r.name = e ? (e->name.empty() ? "Episode " + std::to_string(e->number) : e->name) : l.filename;
        r.overview = e ? e->overview : "";
        if (!e) {
            char sz[32];
            snprintf(sz, sizeof sz, "%.1f GB", l.size / 1e9);
            r.overview = (l.quality.empty() ? "" : l.quality + "  \xC2\xB7  ") + sz;
        }
        r.still = e ? e->still : "";
        r.runtime = e ? e->runtime : 0;
        auto p = db().link_progress.find(l.id);
        r.progress = p != db().link_progress.end() && p->second.second > 0 ? (float)p->second.first / p->second.second : 0;
        dt_rows.push_back(r);
    }
    std::sort(dt_rows.begin(), dt_rows.end(), [](const HxEpisodeRow &a, const HxEpisodeRow &b) {
        return a.number != b.number ? a.number < b.number : a.name < b.name;
    });
    if (t && t->resume_link && dt_fresh)
        for (size_t i = 0; i < dt_rows.size(); i++)
            if (dt_rows[i].link_id == t->resume_link)
                dt_episode = (int)i;
    dt_fresh = false;
    if (dt_episode >= (int)dt_rows.size())
        dt_episode = 0;
}

void hx_categories_open(int media_id);
void hx_poster_prompt(int media_id);

void hx_detail_more(const HxTitle &t)
{
    int id = t.id;
    std::string title = t.title;
    hx_open_menu(t.title, "", { "Hide from library", "Unmatch (back to Manage)", "Set a poster address", "Cancel" }, [id, title](int i) {
        if (i == 0) {
            hx_confirm("Hide " + title + "?", "It leaves Home and Search. Show it again from Manage library.", "Hide", [id] {
                set_media_hidden(id, true);
                hx_go_back();
                toast("Hidden. Manage library brings it back", 3);
            });
        } else if (i == 1) {
            hx_confirm("Unmatch " + title + "?", "Its files go back to Manage library, unmatched, to match again.", "Unmatch", [id] {
                unmatch_media(id);
                hx_go_back();
            });
        } else if (i == 2) {
            hx_poster_prompt(id);
        }
    }, 0);
}

void hx_detail_screen()
{
    const HxTitle *t = hx_title(dt_media);
    if (!t) {
        hx_go_back(); /* hidden or unmatched meanwhile */
        return;
    }
    hx_build_detail();
    bool input = hx_dialog == D_NONE;
    float fa = ease(hx_fade);

    rect(0, 0, 1920, 1080, NF_BG);
    std::string bg = db_image_url(t->backdrop, "w1280");
    float bx = 480, bw = 1440, bh = bw * 9 / 16;
    if (!bg.empty())
        hx_cover(bg, bx, 0, bw, bh, fa);
    hgrad(bx, 0, 760, bh, NF_BG, alpha(NF_BG, 0));
    dgrad(bx, bh - 420, bw, 420, alpha(NF_BG, 0), NF_BG);
    rect(0, bh, 1920, 1080 - bh, NF_BG);
    hx_logo(72, 34, 50, fa);

    bool tv = !dt_rows.empty() && (dt_seasons.size() > 1 || dt_seasons[0].first >= 0);
    bool resume = t->resume_link && (t->resume_pos > 0 || t->resume_label.size());
    std::string play_label = resume && t->resume_pos > 0 ? (t->resume_label.empty() ? "Resume" : "Resume " + t->resume_label)
                           : t->resume_label.empty() ? "Play" : "Play " + t->resume_label;
    std::vector<std::pair<std::string, int>> buttons = { { play_label, 1 }, { "My List", 3 }, { "More", 4 } };

    hx_title_info(*t, 96, 150, 900, fa, 4);
    float by = 560;
    float bxx = 96;
    for (size_t i = 0; i < buttons.size(); i++) {
        float w = i == 0 ? std::max(250.0f, hx_size(f_bold, 27, buttons[i].first).x + 110) : i == 1 ? 230 : 190;
        float f = focus_anim(hash_id(buttons[i].first, 7600 + i), input && dt_zone == 0 && dt_button == (int)i, 16);
        hx_button(bxx, by, w, 72, buttons[i].first.c_str(), f, fa, buttons[i].second);
        bxx += w + 20;
    }
    if (t->progress > 0.01f && resume)
        hx_progress(96, by + 92, 250, t->progress, fa);

    /* seasons and episodes (or the files of a film) */
    float sy = 690;
    int ns = (int)dt_seasons.size();
    if (ns > 1 || tv) {
        float sx = 96;
        for (int i = 0; i < ns; i++) {
            const std::string &name = dt_seasons[i].second;
            float w = hx_size(f_semi, 24, name).x + 40;
            bool cur = i == dt_season_i;
            float f = focus_anim(hash_id(name, 7700 + (uint64_t)i), input && dt_zone == 1 && cur, 16);
            rect(sx, sy, w, 48, alpha(mix(cur ? IM_COL32(70, 70, 70, 255) : IM_COL32(40, 40, 40, 255), NF_WHITE, f), fa), 24);
            hx_text(f_semi, 24, sx + 20, sy + 10, alpha(mix(cur ? NF_WHITE : NF_DIM, NF_BLACK, f), fa), name);
            sx += w + 12;
        }
    } else {
        text(f_bold, 28, 96, sy + 6, alpha(NF_WHITE, fa), dt_rows.size() > 1 ? "Files" : "");
    }
    float ey = 768, ew = 380, egap = 24;
    dt_ep_scroll = approach(dt_ep_scroll, (float)std::max(0, dt_episode - 1), 12);
    for (int i = 0; i < (int)dt_rows.size(); i++) {
        float x = 96 + (i - dt_ep_scroll) * (ew + egap);
        const HxEpisodeRow &r = dt_rows[i];
        bool foc = input && dt_zone == 2 && dt_episode == i;
        if (x > 1920 || (x < 0 && !foc))
            continue;
        float f = focus_anim(hash_id(r.name, 7800 + (uint64_t)r.link_id), foc, 16);
        float ia = fa * (x < 60 && !foc ? std::max(0.0f, (x + ew) / (ew + 60)) : 1);
        float imgh = 150;
        std::string still = r.still.empty() ? "" : db_image_url(r.still, "w300");
        if (still.empty() || !hx_cover(still, x, ey, ew, imgh, ia, 6)) {
            if (!bg.empty() && hx_cover(bg, x, ey, ew, imgh, ia * 0.5f, 6)) {
            } else {
                rect(x, ey, ew, imgh, alpha(tint_for(r.name, 0.3f), ia), 6);
            }
        }
        if (r.number > 0) {
            rect(x + 10, ey + 10, 52, 34, alpha(NF_BLACK, 0.7f * ia), 4);
            hx_text(f_bold, 22, x + 36, ey + 15, alpha(NF_WHITE, ia), std::to_string(r.number), 0.5f);
        }
        if (r.progress > 0.01f)
            hx_progress(x, ey + imgh - 5, ew, r.progress, ia);
        if (f > 0.01f) {
            stroke(x - 3, ey - 3, ew + 6, imgh + 6, alpha(NF_WHITE, f * ia), 8, 4);
            icon_play(x + ew / 2, ey + imgh / 2, 48 * f, alpha(NF_WHITE, f * ia));
        }
        hx_text(f_bold, 22, x, ey + imgh + 12, alpha(NF_WHITE, ia), r.name, 0, ew - (r.runtime ? 70 : 0));
        if (r.runtime)
            hx_text(f_semi, 20, x + ew, ey + imgh + 14, alpha(NF_DIM, ia), std::to_string(r.runtime) + "m", 1);
        hx_wrapped(f_reg, 19, x, ey + imgh + 44, alpha(NF_DIM, ia), r.overview, ew, 2, 1.3f);
    }

    if (!input)
        return;
    int nb = (int)buttons.size();
    if (hit(PAD_CIRCLE)) {
        hx_go_back();
        return;
    }
    if (dt_zone == 0) {
        if (hit(PAD_LEFT) && dt_button > 0)
            dt_button--;
        if (hit(PAD_RIGHT) && dt_button + 1 < nb)
            dt_button++;
        if (hit(PAD_DOWN) && !dt_rows.empty())
            dt_zone = ns > 1 || tv ? 1 : 2;
        if (hit(PAD_CROSS)) {
            if (dt_button == 0) {
                if (t->resume_link)
                    hx_play(t->resume_link, t->resume_pos * 1000);
                else if (!dt_rows.empty())
                    hx_play(dt_rows[0].link_id, hx_resume_ms(dt_rows[0].link_id));
            } else if (dt_button == 1) {
                hx_categories_open(t->id);
            } else {
                hx_detail_more(*t);
            }
        }
    } else if (dt_zone == 1) {
        if (hit(PAD_LEFT) && dt_season_i > 0) {
            dt_season_i--;
            dt_episode = 0;
        }
        if (hit(PAD_RIGHT) && dt_season_i + 1 < ns) {
            dt_season_i++;
            dt_episode = 0;
        }
        if (hit(PAD_UP))
            dt_zone = 0;
        if (hit(PAD_DOWN) || hit(PAD_CROSS))
            dt_zone = 2;
    } else {
        int n = (int)dt_rows.size();
        if (hit(PAD_LEFT) && dt_episode > 0)
            dt_episode--;
        if (hit(PAD_RIGHT) && dt_episode + 1 < n)
            dt_episode++;
        if (hit(PAD_UP))
            dt_zone = ns > 1 || tv ? 1 : 0;
        if ((hit(PAD_L1) || hit(PAD_R1)) && ns > 1) {
            dt_season_i = (dt_season_i + (hit(PAD_R1) ? 1 : ns - 1)) % ns;
            dt_episode = 0;
        }
        if (hit(PAD_CROSS) && dt_episode < n)
            hx_play(dt_rows[dt_episode].link_id, hx_resume_ms(dt_rows[dt_episode].link_id));
        if (hit(PAD_TRIANGLE) && dt_episode < n)
            hx_play(dt_rows[dt_episode].link_id, 0);
    }
    if (dt_zone == 2)
        hints(1824, 1050, { { PAD_CROSS, "Play" }, { PAD_TRIANGLE, "From the start" }, { PAD_CIRCLE, "Back" } });
    else
        hints(1824, 1050, { { PAD_CROSS, "Select" }, { PAD_CIRCLE, "Back" } });
}

/* My List: the user's own categories, ticked for this title */
int cat_media, cat_focus;

void hx_categories_open(int media_id)
{
    cat_media = media_id;
    cat_focus = 0;
    hx_dialog = D_CATEGORIES;
    hx_dialog_anim = 0;
    tick();
}

void hx_categories_draw()
{
    std::vector<DbCategory> cats;
    {
        auto g = db_lock();
        cats = db().categories;
    }
    float a = ease(hx_dialog_anim);
    rect(0, 0, 1920, 1080, alpha(NF_BLACK, 0.72f * a));
    int n = (int)cats.size() + 2; /* + New list, Done */
    float w = 760, h = 150 + std::min(n, 9) * 72, x = 960 - w / 2, y = 540 - h / 2 + (1 - a) * 30;
    rect(x, y, w, h, alpha(IM_COL32(24, 24, 24, 252), a), 10);
    text(f_bold, 32, x + 48, y + 40, alpha(NF_WHITE, a), "My List");
    text(f_reg, 22, x + 48, y + 86, alpha(NF_DIM, a), "Tick the lists this title belongs in");
    if (hit(PAD_UP))
        cat_focus = (cat_focus + n - 1) % n;
    if (hit(PAD_DOWN))
        cat_focus = (cat_focus + 1) % n;
    int first = std::max(0, std::min(cat_focus - 4, n - 9));
    for (int i = first; i < n && i < first + 9; i++) {
        float ry = y + 130 + (i - first) * 72;
        std::string label = i < (int)cats.size() ? cats[i].name : i == (int)cats.size() ? tr("+ New list") : tr("Done");
        float f = focus_anim(hash_id(label, 7900 + (uint64_t)i), i == cat_focus, 18);
        rect(x + 32, ry, w - 64, 60, alpha(mix(IM_COL32(40, 40, 40, 255), NF_WHITE, f), a), 6);
        ImU32 ink = alpha(mix(NF_WHITE, NF_BLACK, f), a);
        if (i < (int)cats.size()) {
            bool on = category_has(cat_media, cats[i].id);
            stroke(x + 56, ry + 16, 28, 28, ink, 4, 2);
            if (on)
                icon_check(x + 70, ry + 30, 22, ink);
            hx_text(f_semi, 25, x + 104, ry + 15, ink, label, 0, w - 170);
        } else {
            hx_text(f_semi, 25, x + 64, ry + 15, ink, label, 0, w - 128);
        }
    }
    hints(x + w - 40, y + h + 40, { { PAD_CROSS, "Select" }, { PAD_SQUARE, "Delete list" }, { PAD_CIRCLE, "Done" } }, a);
    if (hit(PAD_CROSS)) {
        if (cat_focus < (int)cats.size()) {
            category_toggle(cat_media, cats[cat_focus].id);
            tick();
        } else if (cat_focus == (int)cats.size()) {
            int media = cat_media;
            hx_open_prompt("New list", "A name, like Favourites or Watch with kids", "", false, [media](const std::string &name) {
                int id = category_create(name);
                if (id && !category_has(media, id))
                    category_toggle(media, id);
                hx_categories_open(media);
            });
        } else {
            hx_dialog = D_NONE;
        }
    } else if (hit(PAD_SQUARE) && cat_focus < (int)cats.size()) {
        int id = cats[cat_focus].id, media = cat_media;
        hx_confirm("Delete the list " + cats[cat_focus].name + "?", "The titles stay in your library.", "Delete", [id, media] {
            category_delete(id);
            hx_categories_open(media);
        });
    } else if (hit(PAD_CIRCLE)) {
        hx_dialog = D_NONE;
    }
}

void hx_poster_prompt(int media_id)
{
    hx_open_prompt("Poster address", "An image address (https://...). Leave it empty to remove a custom poster.", "", true,
                   [media_id](const std::string &url) {
                       set_media_poster(media_id, url);
                       toast(url.empty() ? "Poster removed" : "Poster saved", 2.5);
                   }, "https://");
}

/* ---- settings ------------------------------------------------------------------- */

int st_focus;

enum { S_ALLDEBRID, S_TMDB, S_SCAN, S_MANAGE, S_PHONE, S_FILTER, S_PLAYER, S_CLEAR, S_SIGNOUT, S_ABOUT, S_QUIT, S_ROWS };

void hx_ask_alldebrid_key()
{
    hx_open_prompt("AllDebrid API key", "From alldebrid.com/apikeys. Or paste it from your phone (Phone & PC page).", "", false,
                   [](const std::string &key) {
                       if (key.empty())
                           return;
                       keys_check_start(key, "");
                       hx_wait("Checking your AllDebrid key\xE2\x80\xA6", nullptr);
                   }, "AllDebrid API key");
}

void hx_ask_tmdb_key()
{
    hx_open_prompt("TMDB API key", "From themoviedb.org/settings/api: the API key or the read access token.", "", false,
                   [](const std::string &key) {
                       if (key.empty())
                           return;
                       std::string ad;
                       {
                           auto g = db_lock();
                           ad = db().alldebrid_key;
                       }
                       keys_check_start(ad, key);
                       hx_wait("Checking your TMDB key\xE2\x80\xA6", nullptr);
                   }, "TMDB API key");
}

void hx_settings_screen()
{
    rect(0, 0, 1920, 1080, NF_BG);
    hx_logo(72, 34, 50);
    text(f_bold, 48, 96, 130, NF_WHITE, "Settings");
    bool input = hx_dialog == D_NONE;

    std::string ad_user, ad_key, terms;
    bool tmdb_saved;
    int titles = (int)hx_all.size(), files = 0, unmatched = 0;
    int64_t last_scan;
    {
        auto g = db_lock();
        ad_user = db().alldebrid_user;
        ad_key = db().alldebrid_key;
        tmdb_saved = !db().tmdb_key.empty();
        terms = db().block_terms;
        last_scan = db().last_scan;
        for (const DbLink &l : db().links) {
            files++;
            if (!l.matched)
                unmatched++;
        }
    }
    JobStatus scan = scan_status();
    auto masked = [](const std::string &k) { return k.size() > 6 ? k.substr(0, 3) + "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2" + k.substr(k.size() - 3) : k; };
    char when[64] = "";
    if (last_scan) {
        time_t ts = (time_t)last_scan;
        struct tm tmv;
        localtime_r(&ts, &tmv);
        strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tmv);
    }
    struct Row {
        const char *label;
        std::string value;
    } rows[S_ROWS] = {
        { "AllDebrid account", ad_user.empty() ? masked(ad_key) : ad_user + "  (" + masked(ad_key) + ")" },
        { "TMDB key", tmdb_saved ? tr("Saved") : tmdb_has_builtin_key() ? tr("Built in") : tr("Not set: titles can't be matched") },
        { "Scan AllDebrid library", scan.state == JOB_BUSY ? scan.message : !scan.message.empty() ? scan.message
                                    : last_scan ? trf("Last scan %s", when) : tr("Never scanned") },
        { "Manage library", trf("%d titles, %d files, %d unmatched", titles, files, unmatched) },
        { "Phone & PC page", web_running() ? web_address() : tr("Off") },
        { "Content filter", terms.empty() ? tr("Off") : terms },
        { "Player settings", tr("Subtitles, audio, picture, decoding (VLC)") },
        { "Clear library", tr("Forget every title and file (keys stay)") },
        { "Remove AllDebrid key", tr("Back to the first page") },
        { "About HixFlix", "v" VLC_PS5_VERSION },
        { "Quit HixFlix", "" },
    };
    if (input) {
        if (hit(PAD_UP))
            st_focus = (st_focus + S_ROWS - 1) % S_ROWS;
        if (hit(PAD_DOWN))
            st_focus = (st_focus + 1) % S_ROWS;
        if (hit(PAD_CIRCLE)) {
            hx_go_back();
            return;
        }
    }
    float y0 = 230, rh = 70;
    float top = std::max(0.0f, (st_focus - 8) * rh);
    for (int i = 0; i < S_ROWS; i++) {
        float y = y0 + i * rh - top;
        if (y < 200 || y > 1000)
            continue;
        float f = focus_anim(hash_id(rows[i].label, 8000), st_focus == i, 16);
        rect(96, y, 1728, rh - 10, alpha(mix(IM_COL32(32, 32, 32, 255), NF_WHITE, f), 1), 6);
        ImU32 ink = mix(NF_WHITE, NF_BLACK, f), dim = mix(NF_DIM, IM_COL32(60, 60, 60, 255), f);
        text(f_semi, 27, 132, y + 14, (i == S_CLEAR || i == S_SIGNOUT) ? mix(IM_COL32(255, 120, 120, 255), NF_RED, f) : ink, rows[i].label);
        hx_text(f_reg, 23, 1788, y + 17, dim, rows[i].value, 1, 1000);
        if (i == S_SCAN && scan.state == JOB_BUSY && scan.progress >= 0) {
            rect(132, y + rh - 18, 1656, 4, IM_COL32(70, 70, 70, 255));
            rect(132, y + rh - 18, 1656 * scan.progress, 4, NF_RED);
        }
    }
    if (input)
        hints(1824, 1050, { { PAD_CROSS, "Select" }, { PAD_CIRCLE, "Back" } });
    if (!input || !hit(PAD_CROSS))
        return;
    switch (st_focus) {
    case S_ALLDEBRID: hx_ask_alldebrid_key(); break;
    case S_TMDB: hx_ask_tmdb_key(); break;
    case S_SCAN:
        if (scan.state == JOB_BUSY)
            toast("A scan is running", 2);
        else
            scan_start();
        break;
    case S_MANAGE: hx_go(HX_MANAGE); break;
    case S_PHONE:
        hx_dialog = D_PHONE;
        hx_dialog_anim = 0;
        break;
    case S_FILTER:
        hx_open_prompt("Content filter", "Words, separated by commas: files whose names have one are left out of scans (and removed).",
                       terms, false, [](const std::string &t) {
                           auto g = db_lock();
                           db().block_terms = t;
                           db_save_soon();
                           toast("Saved: the next scan uses it", 2.5);
                       }, "word, another word");
        break;
    case S_PLAYER:
        modal = SETTINGS;
        modal_focus = 0;
        modal_anim = 0;
        settings_cat = 0;
        settings_inside = false;
        break;
    case S_CLEAR:
        hx_confirm("Clear the library?", "Every title, file, list and watch position is forgotten. Your keys stay; scan again to rebuild it.",
                   "Clear", [] {
                       auto g = db_lock();
                       db_clear_library();
                       db_save_soon();
                       toast("Library cleared", 2.5);
                   });
        break;
    case S_SIGNOUT:
        hx_confirm("Remove the AllDebrid key?", "HixFlix goes back to the first page. Your library stays on this PS5.", "Remove", [] {
            {
                auto g = db_lock();
                db().alldebrid_key.clear();
                db().alldebrid_user.clear();
                db_save_soon();
            }
            ob_step = 0;
            ob_message.clear();
            kb_open("", false);
            hx_back.clear();
            hx_screen = HX_ONBOARD;
        });
        break;
    case S_ABOUT:
        hx_open_menu("HixFlix v" VLC_PS5_VERSION,
                     trf("Your AllDebrid library on PS5, with VLC's player. Library file: %s. Posters and titles from TMDB "
                         "(this product uses the TMDB API but is not endorsed or certified by TMDB).", db_path().c_str()),
                     { "Close" }, nullptr);
        break;
    case S_QUIT:
        hx_confirm("Quit HixFlix?", "", "Quit", [] {
            /* straight out after saving: VLC's full shutdown waits on its DLNA
             * discovery (it never ran on the console, where the PS button ends apps) */
            player_stop();
            db_flush();
            plat_quit();
        });
        break;
    }
}

/* ---- Manage library (HIX's ManageScreen) ------------------------------------------- */

struct HxGroup {
    std::string show;           /* the parsed name ("" for unparsed) */
    int files = 0, matched = 0, hidden = 0;
    std::string title;          /* the title it's matched to */
    int media_id = 0;
    bool tv = false;
};

std::vector<HxGroup> mg_groups;
int mg_focus, mg_filter, mg_built = -1, mg_built_filter = -1;
float mg_scroll;
const char *const mg_filters[] = { "All", "Unmatched", "Matched", "Hidden" };

void hx_build_groups()
{
    if (mg_built == hx_built && mg_built_filter == mg_filter)
        return;
    mg_built = hx_built;
    mg_built_filter = mg_filter;
    std::map<std::string, HxGroup> m;
    {
        auto g = db_lock();
        for (const DbLink &l : db().links) {
            HxGroup &gr = m[l.parsed_show];
            gr.show = l.parsed_show;
            gr.files++;
            if (l.matched)
                gr.matched++;
            if (l.hidden)
                gr.hidden++;
            if (l.parsed_season >= 0)
                gr.tv = true;
            if (l.matched)
                if (DbMedia *md = db_media(l.media_id)) {
                    gr.title = md->title;
                    gr.media_id = md->id;
                }
        }
    }
    mg_groups.clear();
    for (auto &kv : m) {
        const HxGroup &g = kv.second;
        bool keep = mg_filter == 0 || (mg_filter == 1 && g.matched < g.files) || (mg_filter == 2 && g.matched > 0) ||
                    (mg_filter == 3 && g.hidden > 0);
        if (keep)
            mg_groups.push_back(g);
    }
    /* unmatched first, then the biggest */
    std::sort(mg_groups.begin(), mg_groups.end(), [](const HxGroup &a, const HxGroup &b) {
        bool ua = a.matched < a.files, ub = b.matched < b.files;
        if (ua != ub)
            return ua;
        return a.files != b.files ? a.files > b.files : a.show < b.show;
    });
    if (mg_focus >= (int)mg_groups.size())
        mg_focus = std::max(0, (int)mg_groups.size() - 1);
}

std::string gr_show;            /* HX_GROUP: the group open */
int gr_zone, gr_button, gr_file;
float gr_scroll;

void hx_open_group(const std::string &show)
{
    gr_show = show;
    gr_zone = 0;
    gr_button = 0;
    gr_file = 0;
    gr_scroll = 0;
    hx_go(HX_GROUP);
}

void hx_manage_screen()
{
    hx_build_groups();
    rect(0, 0, 1920, 1080, NF_BG);
    hx_logo(72, 34, 50);
    text(f_bold, 48, 96, 130, NF_WHITE, "Manage library");
    bool input = hx_dialog == D_NONE;
    /* filter tabs */
    float fx = 96;
    for (int i = 0; i < 4; i++) {
        float w = text_size(f_semi, 22, mg_filters[i]).x + 36;
        bool cur = mg_filter == i;
        rect(fx, 210, w, 44, cur ? NF_WHITE : IM_COL32(40, 40, 40, 255), 22);
        text(f_semi, 22, fx + 18, 220, cur ? NF_BLACK : NF_DIM, mg_filters[i]);
        fx += w + 10;
    }
    JobStatus scan = scan_status();
    if (scan.state == JOB_BUSY)
        hx_text(f_reg, 22, 1824, 220, NF_DIM, scan.message, 1, 900);
    int n = (int)mg_groups.size();
    if (input) {
        if (hit(PAD_UP) && mg_focus > 0)
            mg_focus--;
        if (hit(PAD_DOWN) && mg_focus + 1 < n)
            mg_focus++;
        if (hit(PAD_L2))
            mg_focus = std::max(0, mg_focus - 10);
        if (hit(PAD_R2))
            mg_focus = std::max(0, std::min(n - 1, mg_focus + 10));
        if (hit(PAD_L1) || hit(PAD_R1)) {
            mg_filter = (mg_filter + (hit(PAD_R1) ? 1 : 3)) % 4;
            mg_focus = 0;
            tick();
        }
        if (hit(PAD_CROSS) && n) {
            hx_open_group(mg_groups[mg_focus].show);
            return;
        }
        if (hit(PAD_TRIANGLE)) {
            if (scan_busy())
                toast("A scan is running", 2);
            else
                scan_start();
        }
        if (hit(PAD_CIRCLE)) {
            hx_go_back();
            return;
        }
    }
    if (!n)
        text(f_reg, 26, 96, 300, NF_DIM, mg_filter == 0 ? "No files yet: scan your AllDebrid library (\xE2\x96\xB3)." : "Nothing here.");
    float rh = 84, y0 = 290;
    mg_scroll = approach(mg_scroll, (float)std::max(0, mg_focus - 5), 12);
    for (int i = std::max(0, (int)mg_scroll - 1); i < n; i++) {
        float y = y0 + (i - mg_scroll) * rh;
        if (y > 1000)
            break;
        if (y < 260)
            continue;
        const HxGroup &g = mg_groups[i];
        float f = focus_anim(hash_id(g.show, 8100), mg_focus == i, 16);
        rect(96, y, 1728, rh - 10, mix(IM_COL32(30, 30, 30, 255), IM_COL32(64, 64, 64, 255), f), 6);
        if (f > 0.01f)
            rect(96, y, 6, rh - 10, alpha(NF_RED, f), 2);
        bool unmatched = g.matched < g.files;
        hx_text(f_semi, 26, 126, y + 10, NF_WHITE, g.show.empty() ? tr("(name not recognised)") : g.show, 0, 900);
        std::string info = count_of(g.files, "%d file", "%d files") + (g.hidden ? "  \xC2\xB7  " + trf("%d hidden", g.hidden) : "") +
                           (g.tv ? "  \xC2\xB7  " + std::string(tr("episodes")) : "");
        hx_text(f_reg, 20, 126, y + 44, NF_DIM, info, 0, 900);
        if (unmatched) {
            rect(1500, y + 20, 140, 34, IM_COL32(120, 30, 30, 255), 17);
            text(f_semi, 19, 1570, y + 26, NF_WHITE, g.matched ? "Partly" : "Unmatched", 0.5f);
        }
        if (!g.title.empty())
            hx_text(f_semi, 22, 1794, y + 24, NF_GREEN, g.title, 1, unmatched ? 120 : 600);
    }
    if (input)
        hints(1824, 1050, { { PAD_CROSS, "Open" }, { PAD_TRIANGLE, "Scan" }, { PAD_R1, "Filter" }, { PAD_CIRCLE, "Back" } });
}

/* the files of the open group, newest state from the database */
struct HxFile {
    int id;
    std::string name, quality, title;
    int64_t size;
    bool matched, hidden;
};

std::vector<HxFile> hx_group_files(int *media_id, bool *tv)
{
    std::vector<HxFile> out;
    *media_id = 0;
    *tv = false;
    auto g = db_lock();
    for (const DbLink &l : db().links) {
        if (l.parsed_show != gr_show)
            continue;
        HxFile f{ l.id, l.filename, l.quality, "", l.size, l.matched, l.hidden };
        if (DbMedia *m = db_media(l.media_id)) {
            f.title = m->title;
            *media_id = m->id;
        }
        if (l.parsed_season >= 0)
            *tv = true;
        out.push_back(f);
    }
    std::sort(out.begin(), out.end(), [](const HxFile &a, const HxFile &b) { return a.name < b.name; });
    return out;
}

/* ---- matching (HIX's MatchPanel) ---- */

enum { MM_TV, MM_MOVIE, MM_VIDEO, MM_SERIES };
const char *const mm_modes[] = { "TV show", "Movie", "Custom video", "Custom series" };
int mt_mode, mt_zone, mt_result, mt_season = 1;
std::string mt_query, mt_title, mt_poster;
std::vector<int> mt_links;
int mt_search_serial = -1, mt_match_serial = -1;
float mt_scroll;

void hx_open_match(const std::vector<int> &links, const std::string &name, bool tv)
{
    mt_links = links;
    mt_mode = tv ? MM_TV : MM_MOVIE;
    mt_zone = 1;
    mt_result = 0;
    mt_season = 1;
    mt_query = normalize_name(name);
    mt_title = name;
    mt_poster.clear();
    mt_search_serial = -1;
    mt_match_serial = -1;
    hx_go(HX_MATCH);
    if (!mt_query.empty()) {
        tmdb_search_start(tv, mt_query);
        mt_search_serial = tmdb_search_status().serial;
    }
}

void hx_group_screen()
{
    int media_id;
    bool tv;
    std::vector<HxFile> files = hx_group_files(&media_id, &tv);
    if (files.empty()) {
        hx_go_back();
        return;
    }
    rect(0, 0, 1920, 1080, NF_BG);
    hx_logo(72, 34, 50);
    bool input = hx_dialog == D_NONE;
    hx_text(f_bold, 44, 96, 120, NF_WHITE, gr_show.empty() ? tr("(name not recognised)") : gr_show, 0, 1700);
    int matched = 0, hidden = 0;
    std::vector<int> unmatched_ids, all_ids;
    for (const HxFile &f : files) {
        all_ids.push_back(f.id);
        if (f.matched)
            matched++;
        else
            unmatched_ids.push_back(f.id);
        if (f.hidden)
            hidden++;
    }
    std::string sub = count_of((int)files.size(), "%d file", "%d files") + "  \xC2\xB7  " + trf("%d matched", matched);
    for (const HxFile &f : files)
        if (!f.title.empty()) {
            sub += "  \xC2\xB7  " + std::string(tr("Matched to")) + " " + f.title;
            break;
        }
    hx_text(f_reg, 24, 96, 182, NF_DIM, sub, 0, 1700);

    bool all_hidden = hidden == (int)files.size();
    std::vector<std::string> buttons = { unmatched_ids.empty() ? "Match again" : "Match", all_hidden ? "Show all" : "Hide all", "Delete all" };
    if (media_id) {
        buttons.push_back("Set poster");
        buttons.push_back("Open title");
    }
    float bx = 96;
    for (size_t i = 0; i < buttons.size(); i++) {
        float w = text_size(f_bold, 27, buttons[i].c_str()).x + 64;
        float f = focus_anim(hash_id(buttons[i], 8200 + i), input && gr_zone == 0 && gr_button == (int)i, 16);
        hx_button(bx, 240, w, 64, buttons[i].c_str(), f, 1);
        bx += w + 16;
    }
    int n = (int)files.size();
    if (gr_file >= n)
        gr_file = n - 1;
    float rh = 76, y0 = 350;
    gr_scroll = approach(gr_scroll, (float)std::max(0, gr_file - 6), 12);
    for (int i = std::max(0, (int)gr_scroll - 1); i < n; i++) {
        float y = y0 + (i - gr_scroll) * rh;
        if (y > 1000)
            break;
        if (y < 330)
            continue;
        const HxFile &fl = files[i];
        float f = focus_anim(hash_id(fl.name, 8300), input && gr_zone == 1 && gr_file == i, 16);
        rect(96, y, 1728, rh - 10, mix(IM_COL32(30, 30, 30, 255), IM_COL32(64, 64, 64, 255), f), 6);
        hx_text(f_semi, 22, 126, y + 8, fl.hidden ? NF_FAINT : NF_WHITE, fl.name, 0, 1300);
        char sz[48];
        snprintf(sz, sizeof sz, "%.2f GB", fl.size / 1e9);
        std::string info = std::string(sz) + (fl.quality.empty() ? "" : "  \xC2\xB7  " + fl.quality) +
                           (fl.hidden ? "  \xC2\xB7  " + std::string(tr("hidden")) : "");
        hx_text(f_reg, 19, 126, y + 38, NF_DIM, info, 0, 1300);
        hx_text(f_semi, 20, 1794, y + 22, fl.matched ? NF_GREEN : IM_COL32(255, 120, 120, 255),
                fl.matched ? tr("Matched") : tr("Unmatched"), 1);
    }
    if (!input)
        return;
    hints(1824, 1050, { { PAD_CROSS, "Select" }, { PAD_CIRCLE, "Back" } });
    if (hit(PAD_CIRCLE)) {
        hx_go_back();
        return;
    }
    int nb = (int)buttons.size();
    if (gr_zone == 0) {
        if (hit(PAD_LEFT) && gr_button > 0)
            gr_button--;
        if (hit(PAD_RIGHT) && gr_button + 1 < nb)
            gr_button++;
        if (gr_button >= nb)
            gr_button = nb - 1;
        if (hit(PAD_DOWN))
            gr_zone = 1;
        if (hit(PAD_CROSS)) {
            std::string show = gr_show;
            switch (gr_button) {
            case 0: hx_open_match(unmatched_ids.empty() ? all_ids : unmatched_ids, gr_show, tv); break;
            case 1: set_show_hidden(show, !all_hidden); break;
            case 2:
                hx_confirm(count_of(n, "Delete %d file?", "Delete %d files?"), "They leave HixFlix (not AllDebrid). A scan brings them back while they're on AllDebrid.",
                           "Delete", [show] { delete_show(show); });
                break;
            case 3: hx_poster_prompt(media_id); break;
            case 4: hx_open_detail(media_id); break;
            }
        }
    } else {
        if (hit(PAD_UP)) {
            if (gr_file > 0)
                gr_file--;
            else
                gr_zone = 0;
        }
        if (hit(PAD_DOWN) && gr_file + 1 < n)
            gr_file++;
        if (hit(PAD_CROSS)) {
            HxFile fl = files[gr_file];
            bool group_tv = tv;
            hx_open_menu(fl.name, "", { fl.hidden ? "Show" : "Hide", "Match this file", "Play", "Delete", "Cancel" }, [fl, group_tv](int i) {
                if (i == 0)
                    set_link_hidden(fl.id, !fl.hidden);
                else if (i == 1)
                    hx_open_match({ fl.id }, parse_filename(fl.name).show, group_tv);
                else if (i == 2)
                    hx_play(fl.id, hx_resume_ms(fl.id));
                else if (i == 3)
                    hx_confirm("Delete this file?", fl.name, "Delete", [fl] { delete_link(fl.id); });
            });
        }
    }
}

void hx_match_screen()
{
    rect(0, 0, 1920, 1080, NF_BG);
    hx_logo(72, 34, 50);
    bool input = hx_dialog == D_NONE;
    text(f_bold, 44, 96, 120, NF_WHITE, "Match");
    hx_text(f_reg, 24, 96, 182, NF_DIM, count_of((int)mt_links.size(), "%d file", "%d files") + "  \xC2\xB7  " + mt_title, 0, 1700);

    /* a match that finished: back to the group */
    JobStatus ms = match_status();
    if (mt_match_serial >= 0 && ms.serial == mt_match_serial && ms.state != JOB_BUSY) {
        mt_match_serial = -1;
        if (hx_dialog == D_WAIT)
            hx_dialog = D_NONE;
        toast(ms.message, 4);
        if (ms.state == JOB_DONE) {
            hx_go_back();
            return;
        }
    }

    /* zone 0: the modes; 1: the query (or title); 2: search button / poster / season; 3+: results or Save */
    float mx = 96;
    for (int i = 0; i < 4; i++) {
        float w = text_size(f_semi, 24, mm_modes[i]).x + 40;
        bool cur = mt_mode == i;
        float f = focus_anim(hash_id(mm_modes[i], 8400), input && mt_zone == 0 && cur, 16);
        rect(mx, 240, w, 52, mix(cur ? IM_COL32(80, 80, 80, 255) : IM_COL32(40, 40, 40, 255), NF_WHITE, f), 26);
        text(f_semi, 24, mx + 20, 252, mix(cur ? NF_WHITE : NF_DIM, NF_BLACK, f), mm_modes[i]);
        mx += w + 12;
    }
    bool tmdb_mode = mt_mode == MM_TV || mt_mode == MM_MOVIE;
    std::vector<TmdbResult> results = tmdb_mode ? tmdb_search_results() : std::vector<TmdbResult>();
    JobStatus ss = tmdb_search_status();
    bool searching = ss.state == JOB_BUSY && ss.serial == mt_search_serial;
    struct Field {
        const char *label;
        std::string value;
    };
    std::vector<Field> fields;
    if (tmdb_mode) {
        fields.push_back({ "Search TMDB for", mt_query });
    } else {
        fields.push_back({ "Title", mt_title });
        fields.push_back({ "Poster address (optional)", mt_poster });
        if (mt_mode == MM_SERIES)
            fields.push_back({ "Season", std::to_string(mt_season) });
        fields.push_back({ "", tr(mt_mode == MM_VIDEO ? "Save custom video" : "Create custom series") });
    }
    int nf = (int)fields.size();
    for (int i = 0; i < nf; i++) {
        float y = 320 + i * 90;
        bool foc = input && mt_zone == 1 + i;
        float f = focus_anim(hash_id(fields[i].label + std::to_string(mt_mode), 8500 + (uint64_t)i), foc, 16);
        if (!fields[i].label[0]) {
            hx_button(96, y, 420, 70, fields[i].value.c_str(), f, 1);
            continue;
        }
        rect(96, y, 900, 76, mix(IM_COL32(36, 36, 36, 255), IM_COL32(70, 70, 70, 255), f), 6);
        if (f > 0.01f)
            stroke(96, y, 900, 76, alpha(NF_WHITE, f), 6, 2);
        text(f_reg, 18, 120, y + 8, NF_DIM, fields[i].label);
        std::string v = fields[i].value;
        if (mt_mode == MM_SERIES && i == 2)
            v = "\xE2\x97\x80  " + v + "  \xE2\x96\xB6";
        hx_text(f_semi, 26, 120, y + 34, NF_WHITE, v.empty() ? "\xE2\x80\x94" : v, 0, 850);
    }
    /* the TMDB results */
    int nr = (int)results.size();
    if (tmdb_mode) {
        if (searching) {
            float t = (float)fmod(now * 5.5, 2 * PI_F);
            dl->PathArcTo(P(1400, 500), 36 * S, t, t + PI_F * 1.5f, 40);
            dl->PathStroke(NF_RED, 0, 5 * S);
        } else if (!ss.message.empty() && ss.serial == mt_search_serial) {
            hx_text(f_reg, 24, 1060, 330, ss.state == JOB_FAILED ? IM_COL32(255, 120, 120, 255) : NF_DIM, ss.message, 0, 760);
        }
        if (mt_result >= nr)
            mt_result = std::max(0, nr - 1);
        mt_scroll = approach(mt_scroll, (float)std::max(0, mt_result - 2), 12);
        for (int i = 0; i < nr && !searching; i++) {
            float y = 320 + (i - mt_scroll) * 176;
            if (y < 300 || y > 1000)
                continue;
            const TmdbResult &r = results[i];
            bool foc = input && mt_zone == 2 && mt_result == i;
            float f = focus_anim(hash_id(r.title, 8600 + (uint64_t)r.id), foc, 16);
            rect(1040, y, 784, 160, mix(IM_COL32(30, 30, 30, 255), IM_COL32(70, 70, 70, 255), f), 6);
            if (f > 0.01f)
                stroke(1040, y, 784, 160, alpha(NF_WHITE, f), 6, 2);
            std::string poster = db_image_url(r.poster, "w154");
            if (poster.empty() || !hx_cover(poster, 1052, y + 10, 94, 140, 1, 4))
                rect(1052, y + 10, 94, 140, IM_COL32(50, 50, 50, 255), 4);
            std::string head = r.title + (r.date.size() >= 4 ? "  (" + r.date.substr(0, 4) + ")" : "");
            hx_text(f_bold, 24, 1166, y + 14, NF_WHITE, head, 0, 640);
            hx_wrapped(f_reg, 19, 1166, y + 52, NF_DIM, r.overview, 640, 3, 1.35f);
        }
    }
    if (!input)
        return;
    hints(1824, 1050, { { PAD_CROSS, "Select" }, { PAD_CIRCLE, "Back" } });
    if (hit(PAD_CIRCLE)) {
        hx_go_back();
        return;
    }
    int max_zone = tmdb_mode ? 2 : nf;
    if (mt_zone == 0) {
        if (hit(PAD_LEFT) && mt_mode > 0)
            mt_mode--;
        if (hit(PAD_RIGHT) && mt_mode < 3)
            mt_mode++;
        if ((hit(PAD_LEFT) || hit(PAD_RIGHT)) && (mt_mode == MM_TV || mt_mode == MM_MOVIE) && !mt_query.empty()) {
            tmdb_search_start(mt_mode == MM_TV, mt_query);
            mt_search_serial = tmdb_search_status().serial;
        }
        if (hit(PAD_DOWN))
            mt_zone = 1;
        return;
    }
    if (tmdb_mode && mt_zone == 2) {
        if (hit(PAD_UP)) {
            if (mt_result > 0)
                mt_result--;
            else
                mt_zone = 1;
        }
        if (hit(PAD_DOWN) && mt_result + 1 < nr)
            mt_result++;
        if (hit(PAD_LEFT))
            mt_zone = 1;
        if (hit(PAD_CROSS) && mt_result < nr) {
            match_tmdb_start(mt_mode == MM_TV, results[mt_result].id, mt_links);
            mt_match_serial = match_status().serial;
            hx_wait("Matching to " + results[mt_result].title + "\xE2\x80\xA6", nullptr);
        }
        return;
    }
    if (hit(PAD_UP))
        mt_zone--;
    if (hit(PAD_DOWN) && mt_zone < max_zone && !(tmdb_mode && nr == 0))
        mt_zone++;
    if (tmdb_mode && hit(PAD_RIGHT) && nr)
        mt_zone = 2;
    int field = mt_zone - 1;
    if (mt_mode == MM_SERIES && field == 2) {
        if (hit(PAD_LEFT) && mt_season > 0)
            mt_season--;
        if (hit(PAD_RIGHT) && mt_season < 99)
            mt_season++;
    }
    if (!hit(PAD_CROSS))
        return;
    if (tmdb_mode) {
        bool tvm = mt_mode == MM_TV;
        hx_open_prompt("Search TMDB", tvm ? "The show's name" : "The film's name", mt_query, false, [tvm](const std::string &q) {
            mt_query = q;
            if (!q.empty()) {
                tmdb_search_start(tvm, q);
                mt_search_serial = tmdb_search_status().serial;
                mt_zone = 2;
                mt_result = 0;
            }
        });
    } else if (field == 0) {
        hx_open_prompt("Title", "What to call it", mt_title, false, [](const std::string &t) { mt_title = t; });
    } else if (field == 1) {
        hx_open_prompt("Poster address", "An image address for its poster (optional)", mt_poster, true,
                       [](const std::string &p) { mt_poster = p; }, "https://");
    } else if (field == nf - 1) {
        std::string title = mt_title;
        while (!title.empty() && title.back() == ' ')
            title.pop_back();
        if (title.empty()) {
            toast("Give it a title first", 2.5);
            return;
        }
        int id = mt_mode == MM_VIDEO ? match_custom_video(title, mt_links, mt_poster)
                                     : match_custom_series(title, mt_season, mt_links, mt_poster);
        toast(trf("Matched %d files to %s", (int)mt_links.size(), title.c_str()), 3);
        (void)id;
        hx_go_back();
    }
}

/* ---- the frame -------------------------------------------------------------------- */

/* keys pasted on the phone page, and what the page shows about them */
int hx_web_serial = -1;

/* Keys from outside (the phone page, the key file): checked, then saved. */
void hx_keys_arrived(std::string ad, const std::string &tk, const char *what)
{
    if (ad.empty()) {
        auto g = db_lock();
        ad = db().alldebrid_key;
    }
    if (ad.empty()) {
        toast("The AllDebrid key is missing", 3);
        return;
    }
    keys_check_start(ad, tk);
    hx_web_serial = keys_check_status().serial;
    if (hx_screen == HX_ONBOARD)
        ob_serial = hx_web_serial;
    toast(what, 3);
}

/* The key file: hixflix-keys.txt put in the app's folder with a file manager
 * (FTP, a web file manager...). Works in the sandbox, where the phone page
 * can't listen. "alldebrid=KEY" and "tmdb=KEY" lines, or the AllDebrid key
 * alone; read once, then deleted (the keys don't stay lying around). */
void hx_key_file_tick()
{
    static double next;
    if (now < next)
        return;
    next = now + 2;
    std::string path = std::string(plat_data_dir()) + "/" + HX_KEY_FILE;
    FILE *f = fopen(path.c_str(), "r");
    if (!f)
        return;
    std::string ad, tk;
    char line[700];
    while (fgets(line, sizeof line, f)) {
        std::string l = line;
        while (!l.empty() && isspace((unsigned char)l.back()))
            l.pop_back();
        while (!l.empty() && isspace((unsigned char)l[0]))
            l.erase(0, 1);
        if (l.empty() || l[0] == '#')
            continue;
        size_t eq = l.find('=');
        std::string k = eq == std::string::npos ? "" : lower_text(l.substr(0, eq)), v = eq == std::string::npos ? l : l.substr(eq + 1);
        while (!k.empty() && isspace((unsigned char)k.back()))
            k.pop_back(); /* "alldebrid = KEY" */
        while (!v.empty() && isspace((unsigned char)v[0]))
            v.erase(0, 1);
        if (k == "tmdb" || k == "tmdb_key")
            tk = v;
        else if (k.empty() || k == "alldebrid" || k == "alldebrid_key" || k == "apikey")
            ad = ad.empty() ? v : ad;
    }
    fclose(f);
    remove(path.c_str());
    fprintf(stderr, "hixflix: keys read from %s (AllDebrid %s, TMDB %s)\n", HX_KEY_FILE, ad.empty() ? "no" : "yes",
            tk.empty() ? "no" : "yes");
    if (ad.empty() && tk.empty())
        toast(trf("%s had no key in it", HX_KEY_FILE), 4);
    else
        hx_keys_arrived(ad, tk, "Keys found in " HX_KEY_FILE ": checking\xE2\x80\xA6");
}

void hx_web_frame()
{
    static double next_status;
    JobStatus ks = keys_check_status();
    if (now >= next_status) {
        next_status = now + 0.5;
        WebHixInfo info;
        {
            auto g = db_lock();
            info.has_alldebrid = !db().alldebrid_key.empty();
            info.has_tmdb = !db().tmdb_key.empty() || tmdb_has_builtin_key();
            info.user = db().alldebrid_user;
        }
        info.checking = ks.state == JOB_BUSY;
        info.failed = ks.state == JOB_FAILED;
        info.message = ks.state == JOB_BUSY ? "" : ks.message;
        web_set_hix_info(info);
    }
    std::string ad, tk;
    if (web_take_hix_keys(&ad, &tk))
        hx_keys_arrived(ad, tk, "Keys received from your phone or PC: checking\xE2\x80\xA6");
    hx_key_file_tick();
    /* a check started here (Settings, the phone) that finished */
    static int seen = -1;
    if (ks.state != JOB_BUSY && ks.serial != seen && ks.serial > 0) {
        seen = ks.serial;
        if (hx_dialog == D_WAIT && !hx_pending)
            hx_dialog = D_NONE;
        if (hx_screen != HX_ONBOARD)
            toast(ks.message, 4);
    }
}

void hix_init()
{
    hiximg_init();
    bool has_key;
    {
        auto g = db_lock();
        has_key = !db().alldebrid_key.empty();
    }
    if (has_key) {
        hx_screen = HX_BROWSE;
        hx_tab = T_HOME;
    } else {
        hx_screen = HX_ONBOARD;
        kb_open("", false);
    }
}

/* the library side of a frame: HixFlix's screens and dialogs */
void hix_frame()
{
    hx_rebuild();
    hx_web_frame();
    hx_pending_tick();
    HxDialog before = hx_dialog;
    hx_fade = approach(hx_fade, 1, 9);
    hx_dialog_anim = approach(hx_dialog_anim, hx_dialog == D_NONE ? 0.0f : 1.0f, 16);

    uint32_t keep = pressed;
    if (hx_dialog != D_NONE || modal != NONE)
        pressed = 0; /* the dialog has the buttons */
    if (hx_screen != HX_ONBOARD && hx_dialog == D_NONE && modal == NONE) {
        if (hit(PAD_OPTIONS)) {
            if (hx_screen != HX_SETTINGS)
                hx_go(HX_SETTINGS);
            pressed &= ~PAD_OPTIONS;
        }
        if (hit(PAD_R3) && hx_screen == HX_BROWSE) {
            kb_open("", false);
            sr_focus = -1;
            hx_go(HX_SEARCH);
            pressed &= ~PAD_R3;
        }
    }
    switch (hx_screen) {
    case HX_ONBOARD: hx_onboard(); break;
    case HX_BROWSE: hx_browse_screen(); break;
    case HX_SEARCH: hx_search_screen(); break;
    case HX_DETAIL: hx_detail_screen(); break;
    case HX_SETTINGS: hx_settings_screen(); break;
    case HX_MANAGE: hx_manage_screen(); break;
    case HX_GROUP: hx_group_screen(); break;
    case HX_MATCH: hx_match_screen(); break;
    }
    pressed = keep;
    if (modal != NONE || before == D_NONE)
        pressed = 0; /* a dialog opened this frame: its buttons from the next one */
    switch (hx_dialog) {
    case D_MENU: hx_menu_draw(); break;
    case D_PROMPT: hx_prompt_draw(); break;
    case D_CATEGORIES: hx_categories_draw(); break;
    case D_PHONE: hx_phone_draw(); break;
    case D_WAIT: hx_wait_draw(); break;
    default: break;
    }
    pressed = keep;

    /* a scan going on: a small pill in the corner of every screen */
    JobStatus st = scan_status();
    if (st.state == JOB_BUSY && hx_screen != HX_ONBOARD && !(hx_screen == HX_BROWSE && hx_rows.empty())) {
        std::string msg = st.message;
        float w = std::min(700.0f, hx_size(f_semi, 20, msg).x + 80);
        float x = 1920 - 72 - w, y = 100;
        rect(x, y, w, 44, IM_COL32(32, 32, 32, 235), 22);
        float t = (float)fmod(now * 5.5, 2 * PI_F);
        dl->PathArcTo(P(x + 26, y + 22), 10 * S, t, t + PI_F * 1.5f, 24);
        dl->PathStroke(NF_RED, 0, 3 * S);
        hx_text(f_semi, 20, x + 48, y + 10, NF_WHITE, msg, 0, w - 64);
    }
}

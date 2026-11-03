<h1 align="center">HixFlix</h1>

<p align="center">
  <b>Your AllDebrid library on PlayStation 5, in a Netflix-style interface, played by VLC.</b><br>
  HIX without the login: no account, no Supabase. Everything lives in one file on the console.
</p>

---

## What it is

HixFlix is a native PS5 homebrew app. It takes HIX's features (the AllDebrid scan, TMDB matching,
the library, resume, categories, Manage library) and runs them on the console itself. The player
underneath is [VLC for PS5](../VLC) (libvlc 3.0.24, hardware decoding, HDR, subtitles, surround),
built into the same app.

- **No login.** The first page asks for your **AllDebrid API key**. It's checked with AllDebrid and
  saved locally. Type it with the controller, open `http://<ps5-ip>:8080` on your phone or PC and paste
  it there (that page needs etaHEN or the Helper to lift the sandbox), or put a file named
  `hixflix-keys.txt` in `/data/homebrew/PPSA71420/` with any file manager:

  ```
  alldebrid=YOUR_ALLDEBRID_KEY
  tmdb=YOUR_TMDB_KEY
  ```

  HixFlix picks it up within two seconds (on any screen), checks the keys, saves them and deletes the file.
- **TMDB key** (optional, recommended): posters, backdrops, titles, seasons and episodes. Asked after
  the AllDebrid key, or set it later in Settings. It can also be built in (`HIXFLIX_TMDB_KEY`, below).
- **Local database:** `hixflix-db.json` in the app's folder holds the keys, every AllDebrid file link
  (and the last direct link unlocked for it), the TMDB titles, seasons, episodes and image paths, your
  lists, and where you stopped. Posters are cached in `cache/img/`.
- **Netflix look:** a billboard of the title in focus over its backdrop, rows of posters
  (Continue Watching with progress bars, Recently Added, TV Shows, Movies, Anime, Custom Videos,
  your lists), tabs with rows by genre, search, a title page with seasons and episode cards.
- **Playback:** a file is unlocked on the console's own connection (AllDebrid refuses servers) and
  handed to VLC. Position saves every 15 s and when you leave. The next episode plays at the end, and
  L1/R1 skip episodes. A direct link that expired is unlocked again by itself.

### From HIX, all on the console

| HIX (Supabase) | HixFlix |
|---|---|
| Sign in, profile | None. The AllDebrid key on the first page |
| AllDebrid key in Vault | `hixflix-db.json` (and the phone page) |
| `scan` edge function: ingest magnets (>= 100 MB), auto-match on TMDB | Settings › Scan AllDebrid library (runs automatically on the first start). It also removes files gone from AllDebrid |
| `match` function: TV / movie / custom series / custom video, poster address | Manage library › a title › Match |
| `unlock` on the device, cached `unlocked_url` | The same, reused for 6 hours |
| Hide / unhide / delete files and titles, unmatch | Manage library, and a title's More menu |
| User categories | My List (a title's page) and the My List tab |
| `user_media_state`: Continue Watching, resume file | The same, plus progress per episode |
| NSFW term list | Settings › Content filter (your own words) |

## Install

Same as VLC for PS5: a PS5 with **kstuff** and **[ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus)**.

1. Copy the `PPSA71420` folder (from `dist/PPSA71420.zip`) to `/data/homebrew/`.
2. Start **HixFlix** from the home screen and enter your AllDebrid key.

It installs next to VLC (its own title ID, PPSA71420). It's registered as a game
(`ps5/sce_sys/param.json`), so the Create button can record it: the PS5 blocks recording in media
apps. `param-media.json` is the media-app registration VLC uses, if you'd rather have that.

## Controls

| Where | Button | Action |
|---|---|---|
| Everywhere | D-Pad / left stick | Move |
| | ✕ | Select |
| | ○ | Back |
| | Options | Settings |
| Home and tabs | L1 / R1 | Previous / next tab |
| | △ | Play (resume) the title in focus |
| | L2 / R2 | Jump 8 titles in a row |
| | R3 | Search |
| Title page | L1 / R1 | Previous / next season |
| | △ on an episode | Play from the start |
| Keyboard | □ / △ / R2 | Delete / space / done |
| Player | | VLC for PS5's controls; L1 / R1 previous / next episode |

## Files on the PS5

| Path | What |
|---|---|
| `/data/homebrew/PPSA71420/hixflix-db.json` | The library and your keys |
| `/data/homebrew/PPSA71420/cache/img/` | Posters and backdrops |
| `/data/homebrew/PPSA71420/hixflix.log` | Log of the last session |
| `/data/homebrew/PPSA71420/prefs.txt` | Player settings (VLC's) |

## Building

Docker on a Mac or Linux. The build reuses the PS5 payload SDK, RADV and signer that
`ps5-plume-triangle` already built for the Splinter Cell port, from its Docker volumes
(`scbps5-plume-deps`, `scbps5-plume-build`). Set `PLUME_DIR`, `PLUME_DEPS_VOLUME` or
`PLUME_BUILD_VOLUME` to use others.

```sh
./build.sh            # VLC's libraries (first time: about an hour), libvlc, then dist/PPSA71420.zip
./build.sh --title    # only the app, once the libraries are built
HIXFLIX_TMDB_KEY=... ./build.sh --title   # with a TMDB key built in
./dev.sh bash ps5/check-syntax.sh ui hixlib   # compile some sources only, quickly
./build.sh --host                             # the Linux test build (offscreen, lavapipe)
./dev.sh bash host/run-tour.sh                # screenshots of a made-up library in host/demo/shots/
./dev.sh bash host/run-tour.sh /work/hixflix/host/demo/onboard.txt none   # the first page
python3 ps5/art/make-art.py                   # the icon and backgrounds (Pillow, NumPy)
```

## Releases

Every push to `main` builds HixFlix and publishes the next version as a GitHub release with
`HixFlix-v<version>.zip` (`.github/workflows/release.yml`). `VERSION.txt` holds the major.minor
(`1.0`); each release adds one to the patch of the last tag (`v1.0.0`, `v1.0.1`, ...). Put `1.1` in it
to start `v1.1.0`. The version shows in Settings › About HixFlix.

The build runs on a self-hosted runner on the owner's Mac (label `hixflix`), the machine with the PS5
toolchain. Repository settings it reads:

| Setting | What |
|---|---|
| Variable `PLUME_DIR` | ps5-plume-triangle's folder on that Mac |
| Secret `HIXFLIX_TMDB_KEY` (optional) | a TMDB key built into the releases (anyone with the zip can read it out) |

Locally, `./build.sh --version 1.2.3` makes the same versioned zip.

## Source map

| File | What |
|---|---|
| `src/ui_hix.inc` | HixFlix's screens (included by `ui.cc`; the player, menus and keyboard are VLC-PS5's) |
| `src/hixlib.*` | Scan, auto/manual matching, custom titles, hide/delete, lists, progress, unlock (HIX's edge functions and `lib/api.ts`) |
| `src/hixapi.*` | AllDebrid and TMDB clients |
| `src/hixdb.*` | The local JSON database |
| `src/hiximg.*` | Poster and backdrop downloads, disk cache, textures |
| `src/http.*` | HTTP(S) client and JSON reader (GnuTLS), shared with OpenSubtitles |
| `src/web*.{cc,h}` | The phone and PC page: keys, file transfer, OpenSubtitles |

## Credits

Built on **VLC for PS5** (see its README for the full list: VLC, FFmpeg, PS5_Vulkan, the
ps5-payload-dev SDK, Dear ImGui and more). Metadata and images from
[TMDB](https://www.themoviedb.org/). This product uses the TMDB API but is not endorsed or certified
by TMDB. Netflix is a trademark of Netflix, Inc. HixFlix only borrows the style and is not affiliated.

## License

GPL-3.0, as VLC for PS5 (see `LICENSE`).

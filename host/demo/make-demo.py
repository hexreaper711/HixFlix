#!/usr/bin/env python3
"""A small made-up HixFlix library for the host test build (host/demo/tour.txt).

    python3 host/demo/make-demo.py <app-folder>     writes <app-folder>/hixflix-db.json

TMDB image paths are real (images load without a key); the AllDebrid links are not,
so nothing plays. Includes matched titles, a resume point, a list, and unmatched files.
"""
import json
import os
import sys
import time

now = int(time.time())
media, seasons, episodes, links = [], [], [], []
next_id = [1]


def new():
    next_id[0] += 1
    return next_id[0]


def title(name, kind, tmdb, poster, backdrop, year, vote, genres, overview, anime=False, age=0):
    i = new()
    media.append(dict(id=i, tmdb_id=tmdb, title=name, type=kind, poster=poster, backdrop=backdrop, year=year,
                      vote=vote, genres=genres, overview=overview, anime=anime, created=now - age))
    return i


def link(media_id, filename, episode=0, season=-1, number=-1, show=""):
    i = new()
    links.append(dict(id=i, media_id=media_id, episode_id=episode, magnet_id="1", filename=filename,
                      url="https://alldebrid.com/f/demo%d" % i, quality="1080P", size=2_000_000_000, matched=True,
                      parsed_show=show or filename.split(".")[0], parsed_season=season, parsed_episode=number,
                      created=now))
    return i


def show(name, tmdb, poster, backdrop, year, vote, genres, overview, names, anime=False, age=0):
    m = title(name, "tv", tmdb, poster, backdrop, year, vote, genres, overview, anime, age)
    s = new()
    seasons.append(dict(id=s, media_id=m, number=1, name="Season 1", episode_count=len(names)))
    ids = []
    for n, ep_name in enumerate(names, 1):
        e = new()
        episodes.append(dict(id=e, season_id=s, media_id=m, number=n, name=ep_name, runtime=47,
                             overview="An episode of the demo library."))
        ids.append(link(m, "%s.S01E%02d.1080p.mkv" % (name.replace(" ", "."), n), e, 1, n, name))
    return m, ids


bb, bb_links = show("Breaking Bad", 1396, "/ztkUQFLlC19CCMYHW9o1zWhJRNq.jpg", "/tsRy63Mu5cu8etL1X7ZLyf7UP1M.jpg",
                    2008, 8.9, "18,80",
                    "Walter White, a New Mexico chemistry teacher, is diagnosed with Stage III cancer and given only "
                    "two years to live. He becomes filled with a sense of fearlessness and an unrelenting desire to "
                    "secure his family's financial future at any cost.",
                    ["Pilot", "Cat's in the Bag...", "...And the Bag's in the River", "Cancer Man", "Gray Matter",
                     "Crazy Handful of Nothin'", "A No-Rough-Stuff-Type Deal"], age=10)
got, _ = show("Game of Thrones", 1399, "/1XS1oqL89opfnbLl8WnZY1O1uJx.jpg", "/2OMB0ynKlyIenMJWI2Dy9IWT4c.jpg", 2011,
              8.5, "10765,18,10759", "Seven noble families fight for control of the mythical land of Westeros.",
              ["Winter Is Coming", "The Kingsroad", "Lord Snow"], age=20)
show("Attack on Titan", 1429, "/hTP1DtLGFamjfu8WqjnuQdP1n4i.jpg", "", 2013, 8.7, "16,10765,10759",
     "Humanity lives behind walls to protect itself from giant man-eating Titans.", ["To You, in 2000 Years"],
     anime=True, age=5)
for name, tmdb, poster, backdrop, year, vote, genres, overview in [
    ("Fight Club", 550, "/pB8BM7pdSp6B6Ih7QZ4DrQ3PmJK.jpg", "/hZkgoQYus5vegHoetLkCJzb17zJ.jpg", 1999, 8.4, "18,53",
     "A ticking-time-bomb insomniac and a slippery soap salesman channel primal male aggression into a shocking new "
     "form of therapy."),
    ("Inception", 27205, "/oYuLEt3zVCKq57qu2F8dT7NIa6f.jpg", "/8ZTVqvKDQ8emSGUEMjsS4yHAwrp.jpg", 2010, 8.4,
     "28,878,12", "Cobb, a skilled thief who infiltrates the subconscious of his targets, is offered a chance to "
     "regain his old life."),
    ("The Dark Knight", 155, "/qJ2tW6WMUDux911r6m7haRef0WH.jpg", "/nMKdUUepR0i5zn0y1T4CsSB5chy.jpg", 2008, 8.5,
     "18,28,80,53", "Batman raises the stakes in his war on crime."),
    ("Interstellar", 157336, "/gEU2QniE6E77NI6lCU6MxlNBvIx.jpg", "/xJHokMbljvjADYdit5fK5VQsXEG.jpg", 2014, 8.4,
     "12,18,878", "The adventures of a group of explorers who make use of a newly discovered wormhole."),
]:
    m = title(name, "movie", tmdb, poster, backdrop, year, vote, genres, overview, age=30)
    link(m, "%s.%d.1080p.mkv" % (name.replace(" ", "."), year))
link(title("Holiday 2025", "custom", 0, "", "", 0, 0, "", "", age=1), "holiday.part1.mp4")
# unmatched files, for Manage library
for filename, parsed, season, number in [("One.Pace.S01E01.720p.mp4", "One Pace", 1, 1),
                                         ("One.Pace.S01E02.720p.mp4", "One Pace", 1, 2),
                                         ("Some.Unknown.Movie.2019.mkv", "Some Unknown Movie", -1, -1)]:
    links.append(dict(id=new(), filename=filename, url="https://alldebrid.com/f/x", size=900_000_000,
                      parsed_show=parsed, parsed_season=season, parsed_episode=number, created=now))

resume = bb_links[2]
db = dict(version=1, next_id=next_id[0] + 1,
          settings=dict(alldebrid_key="demo-key-not-real", alldebrid_user="demo", last_scan=now),
          media=media, seasons=seasons, episodes=episodes, links=links,
          categories=[dict(id=9000, name="Favourites")], media_categories=[[bb, 9000], [got, 9000]],
          watch=[dict(media_id=bb, position=1200, length=2820, link_id=resume, watched_at=now - 100)],
          link_progress=[[resume, 1200, 2820]])
out = sys.argv[1] if len(sys.argv) > 1 else "."
os.makedirs(out, exist_ok=True)
with open(os.path.join(out, "hixflix-db.json"), "w") as f:
    json.dump(db, f, indent=1)
print("wrote", os.path.join(out, "hixflix-db.json"))

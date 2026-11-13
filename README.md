# Phantasia 4

The Phantasia 4 multiplayer fantasy game server (Brian Kelly's 1999
client/server rewrite of Edward Estes' Phantasia), updated to build and run
on modern 64-bit Linux and to deploy on [Railway](https://railway.com).

```
phantasia/src/          C server source (make)
phantasia/src/118_client  Java client (standalone desktop app)
phantasia/data/         game data files (monsters, classes, shop, ...)
phantasia/bin/          optional e-mail helper scripts
Dockerfile, railway.json, docker-entrypoint.sh   deployment
```

## Deploying on Railway

1. **Create the service.** In Railway, choose *New Project → Deploy from
   GitHub repo* and select this repository. Railway finds `railway.json` and
   builds the `Dockerfile`.
2. **Add a volume** so accounts and characters survive redeploys. In the
   service, open *Settings → Volumes* (or right-click the service → *Attach
   volume*) and set the mount path to:

   ```
   /app/phantasia/data
   ```

   On first start the server copies the stock game files into the volume.
   Without a volume, all progress is lost on every deploy.
3. **Expose the game port.** Phantasia speaks raw TCP, not HTTP, so it needs a
   TCP proxy, not a domain. Go to *Settings → Networking → TCP Proxy* and
   enter port `43302`. Railway gives you an address like
   `shuttle.proxy.rlwy.net:12345`. Players connect to that address.
4. **Give the website a domain.** In *Settings → Networking*, click
   *Generate Domain*. If Railway asks for a port, enter the one shown in the
   log line `Website listening on port ...` (8080 unless Railway sets `PORT`).
   The site is the old phantasia4.com pages from `public_html/`. Its
   "The Game" page (`play.html`) offers the client download and shows the TCP
   proxy address, which is filled in from Railway's `RAILWAY_TCP_PROXY_DOMAIN`
   and `RAILWAY_TCP_PROXY_PORT` variables.
5. Deploy. The logs show `Phantasia server listening on port 43302`, along with
   connection and error logs.

Keep the service at **one replica**: the game state lives in that one process.

### Environment variables (all optional)

| Variable | Default | Purpose |
| --- | --- | --- |
| `PHANTASIA_PORT` | `43302` | Port the server listens on. Takes priority over `PORT`. |
| `PHANTASIA_PUBLIC_ADDRESS` | Railway TCP proxy | Game address shown on the website's play page. |
| `PHANTASIA_EVENT_MINUTES` | `90` | Average minutes between world events (Blood Moon, Gold Rush, Champion Hunt). |
| `PHANTASIA_EVENT_LENGTH` | `20` | How many minutes a world event lasts. |
| `PHANTASIA_WIZARDS` | – | Game admins, as `account:Character:level` separated by commas (level 1 moderator, 2 apprentice, 3 wizard, 4 full wizard; default 4). Rewrites `data/wizards` on every start. Leave unset to manage that file by hand. |
| `PHANTASIA_MOTD` | – | Replaces the message of the day on startup. |
| `PHANTASIA_EMAIL_CONFIRM` | unset | Set to `1` to require e-mailed confirmation codes for new accounts. This needs a working `mail` command in the image, which Railway does not provide, so leave it unset. |

Password-reset e-mails also need `mail`. Without it, the game tells the
player that the e-mail could not be sent.

### Gameplay additions

The revived realm adds daily quests, champion monsters, world events and 12
new monsters (`src/gameplay.c`). Players can read the details and exact
numbers on the website's "New in the Realm" page (`public_html/whatsnew.html`)
and in the new section of the Monsters page. New monsters live in
`data/extramonsters`. Each one has its stats plus the lowest and highest
circle it appears in. Edit that file to add or change monsters, then restart
the server. Daily quest progress is kept in `data/quests`. Character save
files are unchanged.

Shields, swords and quicksilver are now **gear** (`src/gear.c`). Characters
wear a weapon, armor, helm, boots and trinket, and carry a bag (6 slots to start, upgraded at trading posts up to 50).
Many items have an ability. Eleven one-of-a-kind **artifacts** exist, and
whoever holds each one is shown live on the website's Legends page (served
from `/api/artifacts`). Gear is kept in `data/gear` and artifact holders in
`data/artifacts`. Existing characters' shield, sword and quicksilver are
turned into equal "Worn" gear the first time they play. The player guide
is `public_html/gear.html`.

### Wizards (admins)

The easiest way is the `PHANTASIA_WIZARDS` variable above, for example
`myaccount:MyCharacter:4`. Otherwise, `phantasia/data/wizards` lists the characters that get wizard powers. The live
copy is in the volume at `/app/phantasia/data/wizards`. You can edit it with
`railway ssh` or seed your own before the first deploy.

## Playing in the browser

The website's **The Game** page links to the browser client at `/client/`.
It is plain HTML/JavaScript (`public_html/client/`) and talks to the game
through a WebSocket bridge in `web/server.py`, the same small Python server
that serves the website. Nothing needs installing, and it works on phones.

The bridge does the game's connection handshake for the browser and passes
the player's real IP (from Railway's `X-Forwarded-For`), so the game's
per-address connection limits and bans apply to each player.

## Playing: the downloadable client

The original client was a Java applet. Browsers have not run applets for
years, so it is now a desktop program. You need Java 8 or newer.

Players download `phantasia-client.jar` from the website's "The Game" page
and double-click it; it asks for the server address and remembers it. Or:

```bash
cd phantasia/src/118_client
./build.sh
java -jar phantasia-client.jar shuttle.proxy.rlwy.net:12345
```

You can also pass the host and port as separate arguments
(`java -jar phantasia-client.jar host 12345`), or set them with the
`PHANTASIA_HOST` / `PHANTASIA_PORT` environment variables. If you leave them
out, the client connects to `localhost:43302`.

The status icons are bundled from `phantasia/src/118_client/images/`.

## Running locally

```bash
cd phantasia/src
make PHANTASIA_DIR=$PWD/..   # data/ and log/ are looked up under this dir
mkdir -p ../log
./phantasia
```

Or with Docker:

```bash
docker build -t phantasia .
docker run -p 43302:43302 -v phantasia-data:/app/phantasia/data phantasia
```

## What changed to make it run

The 1999 code assumed 32-bit pointers and LinuxThreads (one process ID per
thread). On a modern 64-bit Linux system that caused crashes and hung
connections. The fixes were:

- Added `prototypes.h`, because implicitly declared functions were truncating
  pointers to 32 bits. Also fixed `Do_malloc`, which never returned its
  pointer, and several calls that passed the wrong number of arguments.
- Sent socket `SIGIO`s and combat wake-ups to the owning thread
  (`F_SETOWN_EX`/`tgkill`). The process-wide `alarm()` timers are replaced by
  per-connection deadlines.
- A client disconnecting (`SIGPIPE`) no longer kills the whole server.
- Fixed a use-after-free when a player thread exits.
- The listening port is now converted with `htons` and can be configured.
  The old code bound to `43302` only because of byte order on x86.
- File paths now come from `PHANTASIA_DIR` (default `/app/phantasia`), not from
  a hard-coded home directory. Server, connection and error logs are also
  copied to stdout.

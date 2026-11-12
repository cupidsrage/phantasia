#!/bin/sh
# Prepare the data and log directories, then start the Phantasia server.
#
# /app/phantasia/data is where accounts, characters and the realm are
# saved.  Mount a persistent volume there (on Railway: a Volume with
# mount path /app/phantasia/data) or progress is lost on every deploy.
set -e

DATA_DIR=/app/phantasia/data
LOG_DIR=/app/phantasia/log
SEED_DIR=/app/seed-data

mkdir -p "$DATA_DIR" "$LOG_DIR"

# copy any missing game files (monsters, charstats, ...) into the volume
for seed in "$SEED_DIR"/*; do
    name=$(basename "$seed")
    if [ ! -e "$DATA_DIR/$name" ]; then
        cp "$seed" "$DATA_DIR/$name"
        echo "Seeded $DATA_DIR/$name"
    fi
done

# override the message of the day without rebuilding the image
if [ -n "$PHANTASIA_MOTD" ]; then
    printf '%s\n' "$PHANTASIA_MOTD" > "$DATA_DIR/motd"
fi

# the game keeps its own port so it never collides with the website's $PORT
export PHANTASIA_PORT=${PHANTASIA_PORT:-43302}

# ---- website ----
WEB_DIR=/app/public_html
# the website uses $PORT, unless that is missing or is the game's own port
WEB_PORT=${PHANTASIA_WEB_PORT:-${PORT:-8080}}
if [ "$WEB_PORT" = "$PHANTASIA_PORT" ]; then
    WEB_PORT=8080
fi

# the address players connect the client to: set PHANTASIA_PUBLIC_ADDRESS,
# or let Railway's TCP proxy variables fill it in
GAME_ADDRESS=${PHANTASIA_PUBLIC_ADDRESS:-}
if [ -z "$GAME_ADDRESS" ] && [ -n "${RAILWAY_TCP_PROXY_DOMAIN:-}" ]; then
    GAME_ADDRESS="$RAILWAY_TCP_PROXY_DOMAIN:${RAILWAY_TCP_PROXY_PORT:-43302}"
fi

# build the client downloads with the address built in
if [ -f /app/client/configure-client.py ] && command -v python3 >/dev/null 2>&1; then
    python3 /app/client/configure-client.py "$GAME_ADDRESS" \
        /app/client/phantasia-client.jar /app/client/Phantasia-Windows-base.zip \
        "$WEB_DIR" || echo "Could not prepare the client downloads"
fi

if [ -z "$GAME_ADDRESS" ]; then
    GAME_ADDRESS="(ask the game admin for the address)"
fi

# show the configured world event timing on the "New in the Realm" page
if [ -f "$WEB_DIR/whatsnew.html" ]; then
    sed -i -e "s|@@EVENT_MINUTES@@|${PHANTASIA_EVENT_MINUTES:-90}|g" \
        -e "s|@@EVENT_LENGTH@@|${PHANTASIA_EVENT_LENGTH:-20}|g" "$WEB_DIR/whatsnew.html"
fi

if [ -f "$WEB_DIR/play.html" ]; then
    sed -i "s|@@GAME_ADDRESS@@|$GAME_ADDRESS|g" "$WEB_DIR/play.html"
fi

if [ -d "$WEB_DIR" ] && command -v python3 >/dev/null 2>&1; then
    # website + browser-client bridge; its messages go to the deploy log
    python3 /app/web/server.py --port "$WEB_PORT" --dir "$WEB_DIR" \
        --game-port "$PHANTASIA_PORT" --data-dir "$DATA_DIR" &
    WEB_PID=$!
    sleep 1
    if kill -0 "$WEB_PID" 2>/dev/null; then
        echo "Game address for downloaded clients: $GAME_ADDRESS"
    else
        echo "Website failed to start on port $WEB_PORT (see the error above)"
    fi
fi

exec /app/phantasia/bin/phantasia "$@"

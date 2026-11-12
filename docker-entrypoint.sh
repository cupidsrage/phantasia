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
WEB_PORT=${PORT:-8080}

# the address players connect the client to: set PHANTASIA_PUBLIC_ADDRESS,
# or let Railway's TCP proxy variables fill it in
GAME_ADDRESS=${PHANTASIA_PUBLIC_ADDRESS:-}
if [ -z "$GAME_ADDRESS" ] && [ -n "${RAILWAY_TCP_PROXY_DOMAIN:-}" ]; then
    GAME_ADDRESS="$RAILWAY_TCP_PROXY_DOMAIN:${RAILWAY_TCP_PROXY_PORT:-43302}"
fi
if [ -z "$GAME_ADDRESS" ]; then
    GAME_ADDRESS="(ask the game admin for the address)"
fi

if [ -f "$WEB_DIR/play.html" ]; then
    sed -i "s|@@GAME_ADDRESS@@|$GAME_ADDRESS|g" "$WEB_DIR/play.html"
fi

if [ -d "$WEB_DIR" ] && command -v python3 >/dev/null 2>&1; then
    python3 -m http.server "$WEB_PORT" --bind 0.0.0.0 --directory "$WEB_DIR" \
        >/dev/null 2>&1 &
    echo "Website listening on port $WEB_PORT (game address: $GAME_ADDRESS)"
fi

exec /app/phantasia/bin/phantasia "$@"

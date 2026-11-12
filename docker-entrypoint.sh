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

exec /app/phantasia/bin/phantasia "$@"

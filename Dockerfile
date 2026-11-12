# Phantasia 4 server image (used by Railway, works with plain Docker too)

# ---- build the server ----
FROM debian:bookworm-slim AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc make libc6-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY phantasia/src/ ./
RUN make clean && make PHANTASIA_DIR=/app/phantasia

# ---- runtime ----
FROM debian:bookworm-slim

# perl runs the (optional) account e-mail scripts, tini forwards signals
RUN apt-get update \
    && apt-get install -y --no-install-recommends perl tini \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app/phantasia

COPY --from=build /src/phantasia bin/phantasia
COPY phantasia/bin/*.pl bin/
COPY phantasia/data/ /app/seed-data/
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh

RUN chmod +x bin/* /usr/local/bin/docker-entrypoint.sh \
    && mkdir -p data log

# The Java client connects to 43302 by default.  On Railway, point a
# TCP Proxy at this port.  PHANTASIA_PORT takes priority over PORT.
ENV PHANTASIA_PORT=43302
EXPOSE 43302

ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/docker-entrypoint.sh"]

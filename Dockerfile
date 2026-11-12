# Phantasia 4 server image (used by Railway, works with plain Docker too)

# ---- build the server ----
FROM debian:bookworm-slim AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc make libc6-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY phantasia/src/ ./
RUN make clean && make PHANTASIA_DIR=/app/phantasia

# ---- build the desktop client (served from the website) ----
FROM debian:bookworm-slim AS client

RUN apt-get update \
    && apt-get install -y --no-install-recommends default-jdk-headless \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /client
COPY phantasia/src/118_client/ ./
RUN sh build.sh

# ---- runtime ----
FROM debian:bookworm-slim

# perl runs the (optional) account e-mail scripts, tini forwards signals,
# python3 serves the website
RUN apt-get update \
    && apt-get install -y --no-install-recommends perl tini python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app/phantasia

COPY --from=build /src/phantasia bin/phantasia
COPY phantasia/bin/*.pl bin/
COPY phantasia/data/ /app/seed-data/
COPY public_html/ /app/public_html/
COPY --from=client /client/phantasia-client.jar /app/public_html/phantasia-client.jar
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh

RUN chmod +x bin/* /usr/local/bin/docker-entrypoint.sh \
    && mkdir -p data log

# The game listens on 43302: on Railway, point a TCP Proxy at it.
# The website listens on $PORT (8080 if unset): give it a Railway domain.
ENV PHANTASIA_PORT=43302
EXPOSE 43302 8080

ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/docker-entrypoint.sh"]

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
       ca-certificates curl unzip zip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /client
COPY phantasia/src/118_client/ ./
RUN sh build.sh

# Windows package: a Java runtime plus a launcher, so players don't have to
# install Java.  The jar (with the server address) is added at startup.
ARG WINDOWS_JRE_URL=https://api.adoptium.net/v3/binary/latest/21/ga/windows/x64/jre/hotspot/normal/eclipse?project=jdk
COPY client-package/ /client-package/
RUN mkdir -p /pkg/Phantasia \
    && curl -fsSL -o /tmp/jre.zip "$WINDOWS_JRE_URL" \
    && unzip -q /tmp/jre.zip -d /tmp/jre \
    && mv /tmp/jre/* /pkg/Phantasia/jre \
    && test -f /pkg/Phantasia/jre/bin/javaw.exe \
    && cp "/client-package/Play Phantasia.bat" /client-package/README.txt /pkg/Phantasia/ \
    && cd /pkg && zip -qr /client/Phantasia-Windows-base.zip Phantasia \
    && rm -rf /tmp/jre /tmp/jre.zip /pkg

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
COPY --from=client /client/phantasia-client.jar /app/client/phantasia-client.jar
COPY --from=client /client/Phantasia-Windows-base.zip /app/client/Phantasia-Windows-base.zip
COPY client-package/configure-client.py /app/client/configure-client.py
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh

RUN chmod +x bin/* /usr/local/bin/docker-entrypoint.sh \
    && mkdir -p data log

# The game listens on 43302: on Railway, point a TCP Proxy at it.
# The website listens on $PORT (8080 if unset): give it a Railway domain.
ENV PHANTASIA_PORT=43302
EXPOSE 43302 8080

ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/docker-entrypoint.sh"]

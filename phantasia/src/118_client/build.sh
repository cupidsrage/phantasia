#!/bin/sh
# Build the Phantasia desktop client into phantasia-client.jar.
#
#   ./build.sh
#   java -jar phantasia-client.jar your-app.proxy.rlwy.net:12345
#
# Needs a JDK (8 or newer).  Any .gif images placed in ./images are
# bundled into the jar.
set -e
cd "$(dirname "$0")"

rm -rf build
mkdir -p build/classes
javac -nowarn -encoding ISO-8859-1 --release 8 -d build/classes *.java

if [ -d images ]; then
    mkdir -p build/classes/images
    cp images/* build/classes/images/
fi

printf 'Main-Class: pClient\n' > build/manifest.txt
jar cfm phantasia-client.jar build/manifest.txt -C build/classes .
rm -rf build

echo "Built phantasia-client.jar"
echo "Run it with: java -jar phantasia-client.jar <host>[:port]"

#!/usr/bin/env python3
"""Build the client downloads with this server's address built in.

Run at container start, because the address (Railway's TCP proxy) is only
known then.

    configure-client.py ADDRESS SOURCE_JAR WINDOWS_BASE_ZIP WEB_DIR

Writes into WEB_DIR:
  phantasia-client.jar          the client, with /server.txt = ADDRESS
                                (ADDRESS may be empty: the client then asks)
  Phantasia-Windows.zip         WINDOWS_BASE_ZIP (Java + launcher) plus
                                that jar, if WINDOWS_BASE_ZIP exists
"""

import os
import shutil
import sys
import zipfile


def write_jar(address, source_jar, target_jar):
    tmp = target_jar + ".tmp"
    with zipfile.ZipFile(source_jar) as src, \
            zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as dst:
        for item in src.infolist():
            if item.filename != "server.txt":
                dst.writestr(item, src.read(item.filename))
        if address:
            dst.writestr("server.txt", address + "\n")
    os.replace(tmp, target_jar)


def write_windows_zip(base_zip, jar, target_zip):
    tmp = target_zip + ".tmp"
    shutil.copyfile(base_zip, tmp)
    with zipfile.ZipFile(tmp, "a", zipfile.ZIP_DEFLATED) as zf:
        zf.write(jar, "Phantasia/phantasia-client.jar")
    os.replace(tmp, target_zip)


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)

    address, source_jar, base_zip, web_dir = sys.argv[1:]
    jar = os.path.join(web_dir, "phantasia-client.jar")

    write_jar(address, source_jar, jar)
    print("Client download points at %s" % (address or "(no address set)"))

    if os.path.exists(base_zip):
        write_windows_zip(base_zip, jar, os.path.join(web_dir, "Phantasia-Windows.zip"))
        print("Windows download (with Java) is ready")
    else:
        print("No Windows base package found; only the .jar is offered")


if __name__ == "__main__":
    main()

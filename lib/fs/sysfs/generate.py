import os
import sys
import json
import re
import argparse
from typing import TextIO

PRELUDE = """
#include <zephyr/fs/fs.h>
#include <sysfs/sysfs.h>

"""


def pathencode(s, cnames):
    cname = re.sub(r'[^a-z0-9_]', '_', s.lower())

    if not cname or cname[0].isdigit():
        cname = "_" + cname

    while cname in cnames:
        cname += "_"

    cnames.add(cname)
    return cname


def encodeentry(output: TextIO, path: str, name: str, cname: str, parent: str, public=False):
    typ = ""
    content_name = ""
    content = "NULL"
    size = 0

    def writeline(s=""):
        output.write(s + "\n")

    if os.path.isdir(path):
        entries = sorted(os.listdir(path))

        typ = "FS_DIR_ENTRY_DIR"
        content_name = "entries"
        size = len(entries)

        if entries:
            content = cname + "_entries"
            enameset = set()

            static = "static " if not public else "extern "
            writeline(f"{static}const struct sysfs_entry {cname};")

            entrylines = ',\n\t'.join(
                encodeentry(
                    output,
                    os.path.join(path, entry),
                    entry,
                    cname + "_" + pathencode(entry, enameset),
                    f"&{cname}"
                )
                for entry in entries
            )

            writeline(f"static const struct sysfs_entry *const {cname}_entries[] = {{")
            writeline("\t" + entrylines)
            writeline("};")
            writeline()

    elif os.path.isfile(path):
        typ = "FS_DIR_ENTRY_FILE"
        content_name = "content"

        with open(path, 'rb') as file:
            while data := file.read(10):
                if content == "NULL":
                    content = cname + "_content"
                    writeline(f"static const uint8_t {cname}_content[] = {{")

                size += len(data)
                writeline("\t" + ''.join(f'0x{b:02x}, ' for b in data))

        if content != "NULL":
            writeline("};")
            writeline()

    else:
        raise ValueError(f"neither a file nor a directory: {path}")

    static = "static " if not public else ""
    writeline(f"{static}const struct sysfs_entry {cname} = {{")
    writeline(f"\t.type = {typ},")
    writeline(f"\t.name = {json.dumps(name) if name else 'NULL'},")
    writeline(f"\t.parent = {parent},")
    writeline(f"\t.size = {size},")
    writeline(f"\t.{content_name} = {content}")
    writeline("};")

    return f"&{cname}"


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "root",
        nargs="?",
        default=".",
        help="Root directory to embed (default: .)",
    )
    parser.add_argument(
        "-n",
        "--varname",
        default="sysfs_root",
        help="C variable name for the root entry (default: sysfs_root)",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="-",
        help="Output file (default: stdout)",
    )

    args = parser.parse_args()

    output = sys.stdout
    if args.output and args.output != "-":
        output = open(args.output, "w")

    with output:
        print(PRELUDE, file=output)
        encodeentry(output, args.root, "", args.varname, "NULL", True)

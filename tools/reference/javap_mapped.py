#!/usr/bin/env python3
"""Disassemble classes from the 1.21.11 server jar with Mojang names.

Usage: python -I javap_mapped.py <reference_dir> <out_dir> <deobf.Class.Name> [...]

Extracts the (obfuscated) classes, downgrades the class-file version so a
JDK 21 javap can read them, runs `javap -c -p`, then rewrites class, field and
method references through server_mappings.txt. Developer tool only; nothing
produced here is committed.
"""
import os
import re
import subprocess
import sys
import zipfile

PRIM = {"int": "I", "long": "J", "float": "F", "double": "D", "boolean": "Z",
        "byte": "B", "char": "C", "short": "S", "void": "V"}


def load_mappings(path):
    cls_d2o, cls_o2d = {}, {}
    members = []  # (obf_cls, kind, ret/type, name, args, obfname)
    cur = None
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            if not line.startswith(" "):
                m = re.match(r"(\S+) -> (\S+):", line)
                cur = (m.group(1), m.group(2))
                cls_d2o[cur[0]] = cur[1]
                cls_o2d[cur[1]] = cur[0]
                continue
            body = line.strip()
            m = re.match(r"(?:\d+:\d+:)?(\S+) (\S+?)(\(.*\))? -> (\S+)$", body)
            if not m:
                continue
            members.append((cur[1], m.group(1), m.group(2), m.group(3), m.group(4)))
    return cls_d2o, cls_o2d, members


def to_desc(t, cls_d2o):
    dims = 0
    while t.endswith("[]"):
        dims += 1
        t = t[:-2]
    if t in PRIM:
        d = PRIM[t]
    else:
        d = "L" + cls_d2o.get(t, t).replace(".", "/") + ";"
    return "[" * dims + d


def build_member_map(cls_d2o, members):
    mm = {}
    byname = {}
    for ocls, ret, name, args, oname in members:
        if args is None:
            key = (ocls, oname, to_desc(ret, cls_d2o))
        else:
            a = args[1:-1]
            ad = "".join(to_desc(x, cls_d2o) for x in a.split(",")) if a else ""
            key = (ocls, oname, "(" + ad + ")" + to_desc(ret, cls_d2o))
        mm[key] = name
        byname.setdefault((key[1], key[2]), set()).add(name)
    return mm, byname


def remap_desc(desc, cls_o2d):
    return re.sub(r"L([^;]+);", lambda m: "L" + cls_o2d.get(m.group(1).replace("/", "."), m.group(1).replace("/", ".")).replace(".", "/") + ";", desc)


def main():
    ref, out, names = sys.argv[1], sys.argv[2], sys.argv[3:]
    cls_d2o, cls_o2d, members = load_mappings(os.path.join(ref, "server_mappings.txt"))
    mm, byname = build_member_map(cls_d2o, members)
    jar = os.path.join(ref, "bundle", "META-INF", "versions", "1.21.11", "server-1.21.11.jar")
    os.makedirs(out, exist_ok=True)
    zf = zipfile.ZipFile(jar)
    for dn in names:
        on = cls_d2o[dn]
        raw = bytearray(zf.read(on.replace(".", "/") + ".class"))
        raw[6], raw[7] = 0, 65
        cdir = os.path.join(out, "cls")
        os.makedirs(cdir, exist_ok=True)
        cpath = os.path.join(cdir, on + ".class")
        open(cpath, "wb").write(raw)
        txt = subprocess.run(["javap", "-c", "-p", "-constants", cpath], capture_output=True, text=True).stdout

        def ref_sub(m):
            kind, cn, mn, desc = m.group(1), m.group(2), m.group(3), m.group(4)
            ocn = cn.replace("/", ".")
            dcn = cls_o2d.get(ocn, ocn)
            name = mm.get((ocn, mn, desc))
            if name is None:
                cands = byname.get((mn, desc))
                name = next(iter(cands)) if cands and len(cands) == 1 else mn
            return "%s %s.%s:%s" % (kind, dcn, name, remap_desc(desc, cls_o2d))

        txt = re.sub(r"(Method|InterfaceMethod|Field) ([\w/$]+)\.([\w<>$]+):(\S+)", ref_sub, txt)
        txt = re.sub(r"(class) ([\w/$]+)", lambda m: "class " + cls_o2d.get(m.group(2).replace("/", "."), m.group(2)), txt)
        # method headers: name them by declaring-class lookup
        section = []
        grab = False
        with open(os.path.join(ref, "server_mappings.txt"), encoding="utf-8") as mf:
            for line in mf:
                if not line.startswith(" ") and not line.startswith("#"):
                    grab = line.startswith(dn + " -> ")
                if grab and not line.startswith("#"):
                    section.append("// " + line.rstrip().split("(")[0] + ("(" + line.rstrip().split("(", 1)[1] if "(" in line else ""))
        txt = chr(10).join(section) + chr(10) * 2 + txt
        with open(os.path.join(out, dn + ".txt"), "w", encoding="utf-8") as f:
            f.write(txt)
        print("wrote", os.path.join(out, dn + ".txt"))


if __name__ == "__main__":
    main()

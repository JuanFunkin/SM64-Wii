#!/usr/bin/env python3
"""Herramienta de assets de SM64 Wii. Se ejecuta en CI y NO necesita la ROM.

  gen   <repo> <build>             crea los placeholders (texturas, skyboxes, sonido, demos) del tamano correcto
  table <repo> <elf> <nm> <out>    tras enlazar: crea assets.tbl (direcciones en RAM + origen en la ROM US)

assets.tbl (big-endian)
  'SMA2', u32 count, u32 name_table_off
  count x { u32 kind, u32 addr, u32 size, u32 addr2, u32 src, u32 pos, u32 name_off }
  kind 0 = copia directa  (src == 0: pos = offset en la ROM;  src != 0: bloque MIO0 en src, pos dentro del bloque)
  kind 2 = skybox (addr = mosaicos, addr2 = ptrlist de 80 punteros; el blob termina con la tabla de 80 u32)
"""
import json, os, re, struct, subprocess, sys

VER = "us"
DECL = re.compile(r'(?:Texture|u8)\s+(\w+)\s*\[\s*\]\s*=\s*\{\s*$')
INC = re.compile(r'#\s*include\s+"([^"]+\.inc\.c)"')
SKIP = ("textures/skyboxes/", "levels/ending/cake", "sound/", "assets/demos/", "textures/ipl3_raw/")
SKYBOXES = ["bbh", "bidw", "bitfs", "bits", "ccm", "cloud_floor", "clouds", "ssl", "water", "wdw"]
# Bloques de sonido de la ROM US (offset, tamano) -> simbolo.  ctl/tbl salen de assets.json; el resto se midio con la ROM matching.
SOUND = {
    "gSoundDataADSR": ("@sound ctl us", None),
    "gSoundDataRaw":  ("@sound tbl us", None),
    "gMusicData":     (None, (0x7B0860, 0x1BDC0)),
    "gBankSetsData":  (None, (0x7CC620, 0xA0)),
}

def load(repo):
    with open(os.path.join(repo, "assets.json")) as f:
        return json.load(f)

def texture_assets(d):
    out = {}
    for k, v in d.items():
        if k.startswith("@") or k.startswith(SKIP) or not k.endswith(".png"):
            continue
        out[k[:-4] + ".inc.c"] = (k, v)
    return out

def scan_sources(repo, assets):
    found = {}
    for root, _, files in os.walk(repo):
        if "/build" in root or "/tools" in root:
            continue
        for fn in files:
            if not fn.endswith((".c", ".h")):
                continue
            p = os.path.join(root, fn)
            lines = open(p, errors="ignore").read().splitlines()
            for i, ln in enumerate(lines):
                m = INC.search(ln)
                if m and m.group(1) in assets and i > 0:
                    dm = DECL.search(lines[i - 1].strip())
                    if dm:
                        found[m.group(1)] = dm.group(1)
    return found

def sound_blocks(d):
    out = {}
    for sym, (key, fixed) in SOUND.items():
        if key:
            size, locs = d[key]
            out[sym] = (locs[VER][0], size)
        else:
            out[sym] = fixed
    return out

def w(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)

def cmd_gen(repo, build):
    d = load(repo)
    n = 0
    for inc, (png, v) in texture_assets(d).items():       # texturas: [N-1] = 0 fija el tamano del arreglo
        body = "0\n" if inc.startswith("textures/crash_screen/") else f"[{v[2] - 1}] = 0\n"   # la fuente del crash es u32[64]
        w(os.path.join(build, inc), body); n += 1
    sb = sound_blocks(d)                                  # sonido
    names = {"gSoundDataADSR": "sound_data.ctl", "gSoundDataRaw": "sound_data.tbl",
             "gMusicData": "sequences.bin", "gBankSetsData": "bank_sets"}
    for sym, (off, size) in sb.items():
        w(os.path.join(build, "sound", names[sym] + ".inc.c"), f"[{size - 1}] = 0\n")
    for s in SKYBOXES:                                    # skyboxes: mosaicos + lista de 80 punteros
        size = d[f"textures/skyboxes/{s}.png"][0]
        tiles = size - 80 * 4
        w(os.path.join(build, "bin", f"{s}_skybox.c"),
          '#include "types.h"\n#include "macros.h"\n'
          f"ALIGNED8 u8 {s}_skybox_tiles[{tiles}];\n"
          f"const u8 *{s}_skybox_ptrlist[80];\n")
    cake = "".join(f"ALIGNED8 static const Texture cake_end_texture_{i}[] = {{ [3199] = 0 }};\n" for i in range(48))
    w(os.path.join(build, "levels/ending/cake.inc.c"), cake)   # pastel del final: 48 mosaicos de 20x80 rgba16
    for k, v in d.items():                                # demos: la conversion a C lee estos .bin del repo
        if k.startswith("assets/demos/"):
            p = os.path.join(repo, k); os.makedirs(os.path.dirname(p), exist_ok=True)
            if not os.path.exists(p):
                open(p, "wb").write(b"\0" * v[0])
    print(f"placeholders: {n} texturas, {len(sb)} bloques de sonido, {len(SKYBOXES)} skyboxes")

def cmd_table(repo, elf, nm, out):
    d = load(repo)
    assets = texture_assets(d)
    found = scan_sources(repo, assets)
    # Solo las texturas realmente incluidas en esta compilacion (ramas #if de otras regiones fuera):
    # se leen de los archivos .d que genera el compilador.
    bdir = os.path.dirname(os.path.abspath(elf))
    rel = os.path.relpath(bdir, os.path.abspath(repo)) + "/"
    used = set()
    for root, _, files in os.walk(bdir):
        for fn in files:
            if fn.endswith(".d"):
                txt = open(os.path.join(root, fn), errors="ignore").read().replace("\\\n", " ")
                for tok in txt.split():
                    if tok.endswith(".inc.c"):
                        t = tok[len(rel):] if tok.startswith(rel) else tok
                        used.add(t)
    if used:
        found = {k: v for k, v in found.items() if k in used}
    syms = {}
    for ln in subprocess.check_output([nm, elf], text=True).splitlines():
        p = ln.split()
        if len(p) == 3:
            syms.setdefault(p[2], int(p[0], 16))
    ents = []   # (kind, addr, size, addr2, src, pos, name)
    for inc, sym in found.items():
        png, v = assets[inc]
        pos = v[-1].get(VER)
        if pos is None or sym not in syms:
            continue
        src, off = (0, pos[0]) if len(pos) == 1 else (pos[0], pos[1])
        ents.append((0, syms[sym], v[2], 0, src, off, png[:-4]))
    for sym, (off, size) in sound_blocks(d).items():
        if sym in syms:
            ents.append((0, syms[sym], size, 0, 0, off, "sound/" + sym))
    for s in SKYBOXES:
        key = f"textures/skyboxes/{s}.png"
        size, locs = d[key]
        src, off = locs[VER]
        if f"{s}_skybox_tiles" in syms:
            ents.append((2, syms[f"{s}_skybox_tiles"], size, syms[f"{s}_skybox_ptrlist"], src, off, "skybox/" + s))
    csize, clocs = d["levels/ending/cake.png"]
    csrc, coff = clocs[VER]
    for i in range(48):
        sym = f"cake_end_texture_{i}"
        if sym in syms:
            ents.append((0, syms[sym], 3200, 0, csrc, coff + i * 3200, f"cake/{i}"))
    ents.sort(key=lambda e: (e[4], e[5]))               # agrupa por bloque MIO0
    names, name_offs, blob = {}, [], b""
    for e in ents:
        name_offs.append(len(blob)); blob += e[6].encode() + b"\0"
    head = 12 + len(ents) * 28
    with open(out, "wb") as f:
        f.write(b"SMA2" + struct.pack(">II", len(ents), head))
        for e, no in zip(ents, name_offs):
            f.write(struct.pack(">IIIIIII", e[0], e[1], e[2], e[3], e[4], e[5], no))
        f.write(blob)
    print(f"assets.tbl: {len(ents)} entradas")

if __name__ == "__main__":
    c = sys.argv[1]
    if c == "gen": cmd_gen(sys.argv[2], sys.argv[3])
    elif c == "table": cmd_table(*sys.argv[2:6])
    elif c == "scan":
        a = texture_assets(load(sys.argv[2])); f = scan_sources(sys.argv[2], a)
        print(len(a), "texturas,", len(f), "con simbolo")

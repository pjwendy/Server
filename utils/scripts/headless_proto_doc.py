#!/usr/bin/env python3
"""Writes docs/headless-protocol.md from common/proto/headless.proto: every message and enum, in the file's
order, with its fields and the comments the .proto gives them. Run it after changing the .proto:

    python3 utils/scripts/headless_proto_doc.py

Only the standard library; the .proto's own comments are the documentation, so keep them up to date."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROTO = ROOT / "common/proto/headless.proto"
OUT = ROOT / "docs/headless-protocol.md"

FIELD = re.compile(r"^\s*(repeated\s+|optional\s+)?([A-Za-z0-9_.]+)\s+([A-Za-z0-9_]+)\s*=\s*(\d+)\s*(\[[^\]]*\])?\s*;\s*(?://\s*(.*))?$")
ENUMV = re.compile(r"^\s*([A-Z0-9_]+)\s*=\s*(-?\d+)\s*;\s*(?://\s*(.*))?$")
HEAD = re.compile(r"^\s*(message|enum)\s+([A-Za-z0-9_]+)\s*\{(.*)$")
SECTION = re.compile(r"^//\s*-{3,}\s*(.*?)\s*-{3,}\s*$")


def esc(s):
    return (s or "").replace("|", "\\|").strip()


def parse(lines):
    """Yields ('section', title, notes), ('message'|'enum', name, notes, rows)."""
    out, notes, i = [], [], 0
    while i < len(lines):
        line = lines[i].rstrip("\n")
        m = SECTION.match(line.strip())
        if m:
            out.append(("section", m.group(1), []))
            notes = []
            i += 1
            continue
        if line.strip().startswith("//"):
            notes.append(line.strip()[2:].strip())
            i += 1
            continue
        h = HEAD.match(line)
        if not h:
            if not line.strip():
                if notes and out and out[-1][0] == "section" and not out[-1][2]:
                    out[-1] = ("section", out[-1][1], notes)  # (the paragraph under a section's title)
                notes = [] if line.strip() == "" and not (out and out[-1][0] == "section") else notes
                notes = []
            i += 1
            continue
        kind, name, rest = h.group(1), h.group(2), h.group(3)
        rows, oneof, depth = [], None, 1
        body = []
        # a one-line message: `message X { uint32 a = 1; }  // note`
        if "}" in rest:
            inner, _, tail = rest.partition("}")
            body = [inner]
            tail = tail.strip()
            if tail.startswith("//"):
                notes = notes + [tail[2:].strip()]
            depth = 0
        i += 1
        while depth > 0 and i < len(lines):
            l = lines[i].rstrip("\n")
            depth += l.count("{") - l.count("}")
            body.append(l)
            i += 1
        pending = []
        for l in body:
            s = l.strip()
            if not s:
                continue
            if s.startswith("//"):
                pending.append(s[2:].strip())
                continue
            mo = re.match(r"oneof\s+([A-Za-z0-9_]+)\s*\{", s)
            if mo:
                oneof = mo.group(1)
                continue
            if s.startswith("}"):
                oneof = None
                continue
            for part in [p for p in re.split(r";(?=\s*[A-Za-z])", s) if p.strip()] if kind == "message" and s.count(";") > 1 and "//" not in s else [s]:
                part = part if part.rstrip().endswith(";") or "//" in part else part + ";"
                if kind == "message":
                    f = FIELD.match(part)
                    if f:
                        typ = ("repeated " if (f.group(1) or "").strip() == "repeated" else "") + f.group(2)
                        note = " ".join(pending + ([f.group(6)] if f.group(6) else []))
                        rows.append((f.group(3), typ, f.group(4), (f"(one of `{oneof}`) " if oneof else "") + note))
                else:
                    e = ENUMV.match(part)
                    if e:
                        rows.append((e.group(1), e.group(2), " ".join(pending + ([e.group(3)] if e.group(3) else []))))
            pending = []
        out.append((kind, name, notes, rows))
        notes = []
    return out


def render(items):
    names = {it[1] for it in items if it[0] in ("message", "enum")}
    link = lambda t: re.sub(r"\b([A-Z][A-Za-z0-9]+)\b", lambda m: f"[{m.group(1)}](#{m.group(1).lower()})" if m.group(1) in names else m.group(1), t)
    md = ["# The headless protocol: reference",
          "",
          "Generated from [`common/proto/headless.proto`](../common/proto/headless.proto) by "
          "`utils/scripts/headless_proto_doc.py`; don't edit by hand. How the protocol is used (framing, the handshake, "
          "the order of the checks) is in [headless.md](headless.md#the-protocol).",
          "",
          "Every frame is a 4-byte big-endian length, then the protobuf message. The program sends "
          "[ClientFrame](#clientframe); the server sends [ServerFrame](#serverframe).",
          ""]
    toc = [f"- [{it[1]}](#{it[1].lower()})" for it in items if it[0] in ("message", "enum")]
    md += ["<details><summary>All messages and enums</summary>", "", *toc, "", "</details>", ""]
    for it in items:
        if it[0] == "section":
            title = re.sub(r"\s*\(M[0-9.]+\)", "", it[1])  # (the plan's milestone tags aren't for readers)
            md += [f"## {title[0].upper() + title[1:]}", ""]
            if it[2]:
                md += [" ".join(it[2]), ""]
            continue
        kind, name, notes, rows = it
        md += [f"### {name}", ""]
        if notes:
            md += [esc(" ".join(notes)), ""]
        if kind == "message":
            if rows:
                md += ["| Field | Type | # | Notes |", "|---|---|---|---|"]
                md += [f"| `{n}` | {link(t)} | {num} | {esc(note)} |" for n, t, num, note in rows]
            else:
                md += ["*(no fields)*"]
        else:
            md += ["| Value | # | Notes |", "|---|---|---|"]
            md += [f"| `{n}` | {num} | {esc(note)} |" for n, num, note in rows]
        md.append("")
    return "\n".join(md)


if __name__ == "__main__":
    items = parse(PROTO.read_text().splitlines())
    OUT.write_text(render(items))
    n = sum(1 for it in items if it[0] in ("message", "enum"))
    print(f"{OUT.relative_to(ROOT)}: {n} messages and enums")
    sys.exit(0)

#!/usr/bin/env python3
"""Renders docs/architecture.mmd into a light and a dark PNG with transparent backgrounds.

    python3 scripts/render-architecture.py      (needs Node.js; uses @mermaid-js/mermaid-cli)

Pages pick the variant that matches their theme (GitHub: <picture>, website: ThemedImage).
"""
import json
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
MMDC = ["npx", "-y", "-p", "@mermaid-js/mermaid-cli@11.4.2", "mmdc"]

# role -> (fill, stroke, text) per theme
PALETTES = {
    "light": {
        "src": ("#E0F2FE", "#0284C7", "#0C4A6E"), "tool": ("#DBEAFE", "#2563EB", "#1E3A8A"),
        "artifact": ("#FEF3C7", "#D97706", "#78350F"), "key": ("#FFE4E6", "#E11D48", "#881337"),
        "host": ("#F3E8FF", "#9333EA", "#581C87"), "guard": ("#DCFCE7", "#16A34A", "#14532D"),
        "core": ("#16A34A", "#14532D", "#FFFFFF"), "lib": ("#CCFBF1", "#0D9488", "#134E4A"),
        "BUILD": ("#F8FAFC", "#2563EB", "#1E3A8A"), "APP": ("#FDF4FF", "#9333EA", "#581C87"),
        "VM": ("#F0FDF4", "#16A34A", "#14532D"),
        "line": "#64748B", "ship": "#D97706", "flow": "#16A34A", "libline": "#0D9488",
        "hostline": "#9333EA", "keyline": "#E11D48", "labelbg": "#FFFFFF", "labeltext": "#334155",
    },
    "dark": {
        "src": ("#0C2A3E", "#38BDF8", "#E0F2FE"), "tool": ("#172554", "#60A5FA", "#DBEAFE"),
        "artifact": ("#3A2A0B", "#F59E0B", "#FEF3C7"), "key": ("#4C0519", "#FB7185", "#FFE4E6"),
        "host": ("#2E1065", "#C084FC", "#F3E8FF"), "guard": ("#052E16", "#4ADE80", "#DCFCE7"),
        "core": ("#15803D", "#86EFAC", "#FFFFFF"), "lib": ("#042F2E", "#2DD4BF", "#CCFBF1"),
        "BUILD": ("#0F172A", "#60A5FA", "#BFDBFE"), "APP": ("#1A1033", "#C084FC", "#E9D5FF"),
        "VM": ("#05230F", "#4ADE80", "#BBF7D0"),
        "line": "#94A3B8", "ship": "#F59E0B", "flow": "#4ADE80", "libline": "#2DD4BF",
        "hostline": "#C084FC", "keyline": "#FB7185", "labelbg": "#0B1120", "labeltext": "#E2E8F0",
    },
}

# edge indexes, in declaration order in docs/architecture.mmd
SHIP, FLOW, LIB, HOST, KEY = [6], [9, 10, 11], [12, 13], [7, 8, 14], [15]


def palette(p: dict) -> str:
    out = []
    for role in ("src", "tool", "artifact", "key", "host", "guard", "core", "lib"):
        f, s, c = p[role]
        out.append(f"  classDef {role} fill:{f},stroke:{s},stroke-width:2px,color:{c}")
    for sg, extra in (("BUILD", ",stroke-dasharray:6 4"), ("APP", ""), ("VM", "")):
        f, s, c = p[sg]
        out.append(f"  style {sg} fill:{f},stroke:{s},stroke-width:2px,color:{c}{extra}")
    out.append(f"  linkStyle default stroke:{p['line']},stroke-width:2px")
    for idx, color, w in ((SHIP, p["ship"], 4), (FLOW, p["flow"], 3), (LIB, p["libline"], 2),
                          (HOST, p["hostline"], 2), (KEY, p["keyline"], 2)):
        out.append(f"  linkStyle {','.join(map(str, idx))} stroke:{color},stroke-width:{w}px")
    return "\n".join(out)


def main() -> None:
    src = (ROOT / "docs/architecture.mmd").read_text()
    for theme, p in PALETTES.items():
        cfg = {"theme": "base",
               "flowchart": {"curve": "basis", "htmlLabels": True, "nodeSpacing": 36, "rankSpacing": 42, "padding": 14},
               "themeVariables": {"fontFamily": "Inter, Segoe UI, Helvetica, Arial, sans-serif", "fontSize": "15px",
                                  "edgeLabelBackground": p["labelbg"], "lineColor": p["line"],
                                  "textColor": p["labeltext"], "primaryTextColor": p["labeltext"]}}
        with tempfile.TemporaryDirectory() as d:
            mmd, conf = pathlib.Path(d, "a.mmd"), pathlib.Path(d, "c.json")
            mmd.write_text(src.replace("%%PALETTE%%", palette(p)))
            conf.write_text(json.dumps(cfg))
            out = ROOT / f"docs/architecture-{theme}.png"
            subprocess.run(MMDC + ["-c", str(conf), "-i", str(mmd), "-o", str(out), "-s", "2", "-b", "transparent"],
                           check=True)
            print("wrote", out.relative_to(ROOT))


if __name__ == "__main__":
    main()

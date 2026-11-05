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
        "src": ("#F4F5F7", "#9AA1AD", "#1F2329"), "tool": ("#F4F5F7", "#9AA1AD", "#1F2329"),
        "artifact": ("#F4F5F7", "#9AA1AD", "#1F2329"), "key": ("#14161C", "#14161C", "#FFFFFF"),
        "host": ("#F4F5F7", "#9AA1AD", "#1F2329"), "guard": ("#FFF4EE", "#FF6A2B", "#7A2E0E"),
        "core": ("#FF6A2B", "#C2410C", "#14161C"), "lib": ("#F4F5F7", "#9AA1AD", "#1F2329"),
        "BUILD": ("#FAFAFB", "#C9CDD4", "#3A3F4B"), "APP": ("#FFFFFF", "#C9CDD4", "#3A3F4B"),
        "VM": ("#FFF9F5", "#FF6A2B", "#7A2E0E"),
        "line": "#9AA1AD", "ship": "#FF6A2B", "flow": "#FF6A2B", "libline": "#9AA1AD",
        "hostline": "#9AA1AD", "keyline": "#14161C", "labelbg": "#FFFFFF", "labeltext": "#3A3F4B",
    },
    "dark": {
        "src": ("#1B1E26", "#4A505C", "#E5E7EB"), "tool": ("#1B1E26", "#4A505C", "#E5E7EB"),
        "artifact": ("#1B1E26", "#4A505C", "#E5E7EB"), "key": ("#F2F3F5", "#F2F3F5", "#14161C"),
        "host": ("#1B1E26", "#4A505C", "#E5E7EB"), "guard": ("#2A1810", "#FF7A3D", "#FFD9C7"),
        "core": ("#FF6A2B", "#FF8A57", "#14161C"), "lib": ("#1B1E26", "#4A505C", "#E5E7EB"),
        "BUILD": ("#121419", "#3A3F4B", "#C9CDD4"), "APP": ("#121419", "#3A3F4B", "#C9CDD4"),
        "VM": ("#1A1410", "#FF7A3D", "#FFD9C7"),
        "line": "#6B7280", "ship": "#FF7A3D", "flow": "#FF7A3D", "libline": "#6B7280",
        "hostline": "#6B7280", "keyline": "#E5E7EB", "labelbg": "#0E1015", "labeltext": "#E5E7EB",
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

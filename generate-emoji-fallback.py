
from pathlib import Path
from collections import OrderedDict
import subprocess
import unicodedata
from fontTools.ttLib import TTFont

ROOT = Path.cwd()
SOURCES = [ROOT / "README.md", ROOT / "LORE.md"]
OUTPUT = ROOT / "emoji-fallback.tex"

def font_path(family):
    result = subprocess.run(
        ["fc-match", "-f", "%{file}", family],
        check=True, capture_output=True, text=True
    )
    path = result.stdout.strip()
    if not path or not Path(path).is_file():
        raise RuntimeError(f"Font non trovato: {family}")
    return Path(path)

def cmap_for(family):
    font = TTFont(font_path(family))
    try:
        result = set()
        for table in font["cmap"].tables:
            if table.isUnicode():
                result.update(table.cmap.keys())
        return result
    finally:
        font.close()

base_fonts = [
    cmap_for("DejaVu Serif"),
    cmap_for("DejaVu Sans"),
    cmap_for("DejaVu Sans Mono"),
]
emoji = cmap_for("Noto Emoji")
symbols = cmap_for("Noto Sans Symbols 2")

source = "\n".join(path.read_text(encoding="utf-8") for path in SOURCES)
characters = OrderedDict.fromkeys(source)


lines = [
    "% Generato automaticamente da generate-emoji-fallback.py.",
    "% Non modificare manualmente.",
    "",
]


mapped = []
uncovered = []

for char in characters:
    cp = ord(char)
    category = unicodedata.category(char)

    if category.startswith(("C", "M")):
        continue

    # Forza il fallback per simboli tecnici, dingbat ed emoji.
    is_symbol = (
        0x2300 <= cp <= 0x23FF
        or 0x2600 <= cp <= 0x27BF
        or 0x1F000 <= cp <= 0x1FAFF
    )

    if not is_symbol and any(cp in cmap for cmap in base_fonts):
        continue

    if cp in emoji:
        family = r"\NotoEmoji"
    elif cp in symbols:
        family = r"\NotoSymbols"
    else:
        uncovered.append(f"{char} (U+{cp:04X})")
        continue

    replacement = "{" + family + r'\symbol{"' + f"{cp:X}" + "}" + "}"
    lines.append(r"\newunicodechar{" + char + "}{" + replacement + "}")
    mapped.append(f"U+{cp:04X} {char} -> {family}")

OUTPUT.write_text("\n".join(lines) + "\n", encoding="utf-8")

print(f"Fallback scritto: {OUTPUT}")
print(f"Caratteri mappati: {len(mapped)}")
for item in mapped:
    print("  ", item)

print(f"\nCaratteri senza glifo nei font selezionati: {len(uncovered)}")
for item in uncovered:
    print("  ", item)

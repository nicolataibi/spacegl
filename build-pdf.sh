
#!/usr/bin/env bash
set -Eeuo pipefail

cd "$(dirname "$0")"

# Il PDF contiene entrambi i documenti, nell'ordine: README.md, poi LORE.md.
INPUTS=(README.md LORE.md)
OUTPUT_DIR="readme_assets"
OUTPUT_NAME="README.pdf"
OUTPUT="${OUTPUT_DIR}/${OUTPUT_NAME}"
TEX_OUTPUT="README.tex"
PDF_INPUT="$(mktemp --suffix=.md)"
PDF_ASSETS_DIR="pdf-assets"
PDF_ASSETS_MAP="pdf-assets-map.tsv"

trap 'rm -f "$PDF_INPUT"' EXIT

for cmd in pandoc xelatex python3 perl; do
    command -v "$cmd" >/dev/null 2>&1 || {
        printf 'Errore: manca il comando %s\n' "$cmd" >&2
        exit 1
    }
done

for file in "${INPUTS[@]}" cover.tex pdf-style.tex html-assets.lua \
            prepare-pdf-assets.py emoji-fallback.tex; do
    [[ -f "$file" ]] || {
        printf 'Errore: file mancante: %s\n' "$file" >&2
        exit 1
    }
done

printf '%s\n' '===== PREPARAZIONE ASSET ====='
python3 prepare-pdf-assets.py

printf '\n%s\n' '===== PREPARAZIONE README PER PDF ====='

python3 - "$PDF_INPUT" "${INPUTS[@]}" <<'PY'
from pathlib import Path
from urllib.parse import unquote
import re
import sys

REPO_BLOB = "https://github.com/nicolataibi/spacegl/blob/main/"
PAGEBREAK = '<div class="pdf-pagebreak"></div>'

output = Path(sys.argv[1])
sources = [Path(name) for name in sys.argv[2:]]

LABELS = {
    "version": "Versione",
    "license": "Licenza",
    "language": "Linguaggio",
    "graphics": "Grafica",
    "platform": "Piattaforma",
    "status": "Stato",
    "fleet": "Flotta",
    "alliance": "Flotta",
    "encryption": "Cifratura",
    "security": "Cifratura",
}

BADGE_URL = re.compile(r"\((https?://img\.shields\.io/badge/[^)\s]+)\)")
LINK = re.compile(r"(?<!!)\[([^\]]+)\]\(([^)\s]+)\)")
FENCE = re.compile(r"^\s*(```|~~~)")


def badge_label(url):
    """Ricava 'etichetta: valore' dall'URL di un badge shields.io."""
    slug = unquote(url.split("/badge/", 1)[1].split("?", 1)[0])
    slug = re.sub(r"\.(svg|png)$", "", slug)
    slug = slug.replace("--", "\0").replace("__", "\1")
    parts = slug.split("-")
    if len(parts) < 2:
        return None

    def clean(text):
        return (text.replace("_", " ")
                    .replace("\1", "_")
                    .replace("\0", "-")
                    .replace("|", r"\|"))

    name, value = clean(parts[0]), clean(parts[1])
    return f"**{LABELS.get(name.lower(), name)}:** {value}"


def first_h1_id(text):
    """Identificatore GFM del primo titolo di livello 1 fuori dai blocchi di codice."""
    in_fence = False
    for line in text.splitlines():
        if FENCE.match(line):
            in_fence = not in_fence
        elif not in_fence and line.startswith("# "):
            title = re.sub(r"[*_`]", "", line[2:]).strip().lower()
            title = re.sub(r"[^\w\s-]", "", title)
            return re.sub(r"\s", "-", title)
    return None


texts = {path.name: path.read_text(encoding="utf-8") for path in sources}
anchors = {name: first_h1_id(text) for name, text in texts.items()}
stats = {"badge": 0, "internal": 0, "external": 0}


def rewrite_link(match):
    label, target = match.group(1), match.group(2)
    if target.startswith(("http://", "https://", "#", "mailto:")):
        return match.group(0)

    path, _, fragment = target.partition("#")
    clean = unquote(path).lstrip("./")
    name = Path(clean).name

    if name in anchors and anchors[name] and clean == name:
        stats["internal"] += 1
        return f"[{label}](#{anchors[name]})"

    if clean and Path(clean).exists():
        stats["external"] += 1
        suffix = f"#{fragment}" if fragment else ""
        return f"[{label}]({REPO_BLOB}{clean}{suffix})"

    return match.group(0)


def prepare(text):
    result = []
    badges = []
    in_fence = False

    def flush():
        if badges:
            result.append("  ·  ".join(badges))
            badges.clear()

    for line in text.splitlines():
        if FENCE.match(line):
            flush()
            in_fence = not in_fence
            result.append(line)
            continue

        if in_fence:
            result.append(line)
            continue

        found = BADGE_URL.search(line) if "img.shields.io/badge/" in line else None
        if found:
            label = badge_label(found.group(1))
            if label:
                badges.append(label)
                stats["badge"] += 1
                continue

        flush()
        result.append(LINK.sub(rewrite_link, line))

    flush()
    return "\n".join(result)


parts = [prepare(texts[path.name]) for path in sources]
output.write_text(
    f"\n\n{PAGEBREAK}\n\n".join(parts) + "\n",
    encoding="utf-8"
)

print("Documenti uniti: " + " + ".join(path.name for path in sources))
print("Originali preservati.")
print(f"Badge remoti convertiti in etichette testuali: {stats['badge']}")
print(f"Link tra documenti resi interni: {stats['internal']}")
print(f"Link a file del repository resi assoluti: {stats['external']}")
PY

printf '\n%s\n' '===== GENERAZIONE LATEX ====='

pandoc "$PDF_INPUT" \
  --standalone \
  --from=gfm \
  --lua-filter=html-assets.lua \
  --resource-path=. \
  --to=latex \
  --include-before-body=cover.tex \
  --include-in-header=pdf-style.tex \
  --include-in-header=emoji-fallback.tex \
  --number-sections \
  --highlight-style=tango \
  -V lang=it-IT \
  -V papersize=a4 \
  -V geometry:margin=25mm \
  -V documentclass=extarticle \
  -V fontsize=8pt \
  -V mainfont="DejaVu Serif" \
  -V sansfont="DejaVu Sans" \
  -V monofont="DejaVu Sans Mono" \
  -V colorlinks=true \
  -V linkcolor=HeadingBlue \
  -V urlcolor=HeadingBlue \
  -V title="SPACE GL — README e LORE" \
  -V author="Nicola Taibi" \
  -V subject="" \
  --output="$TEX_OUTPUT"

printf '\n%s\n' '===== CONTROLLO ALTEZZA IMMAGINI ====='
perl -pi -e 's/height=\\textheight/height=0.65\\textheight/g' \
    "$TEX_OUTPUT"


python3 - "$TEX_OUTPUT" <<'PY'
from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")

# Riconosce le tabelle Pandoc con specifiche standard:
# @{}lll@{}, @{}ll@{}, @{}llll@{} e varianti.
pattern = re.compile(
    r'\\begin\{longtable\}(?:\[[^\]]*\])?'
    r'\{((?:@\{\})*\s*[lcrX]+\s*(?:@\{\})*)\}'
    r'(.*?)'
    r'\\end\{longtable\}',
    re.DOTALL
)

converted = 0
image_tables = 0

def process_table(match):
    global converted, image_tables

    spec = match.group(1)
    body = match.group(2)
    columns = re.findall(r'[lcrX]', spec)
    count = len(columns)

    # Le gallerie di immagini richiedono una gestione distinta.
    if r'\includegraphics' in body:
        image_tables += 1

        # Quattro immagini al 28% superano la larghezza utile.
        if count == 4:
            body = body.replace(
                r'width=0.28\linewidth',
                r'width=0.22\linewidth'
            )

        return (
            match.group(0)
            .replace(match.group(2), body, 1)
        )

    # Le tabelle testuali con più colonne diventano elastiche.
    if count < 2:
        return match.group(0)

    if count == 2:
        new_spec = r'p{0.24\linewidth}Y'
    elif count == 3:
        new_spec = r'p{0.20\linewidth}YY'
    elif count == 4:
        new_spec = r'p{0.12\linewidth}YYY'
    else:
        new_spec = 'Y' * count

    converted += 1

    return (
        r'\begin{xltabular}{\linewidth}{'
        + new_spec
        + '}'
        + body
        + r'\end{xltabular}'
    )

text = pattern.sub(process_table, text)
path.write_text(text, encoding="utf-8")

print(f"Tabelle testuali convertite: {converted}")
print(f"Tabelle con immagini esaminate: {image_tables}")
PY


if grep -nF 'height=\textheight' "$TEX_OUTPUT"; then
    printf '%s\n' 'Errore: restano vincoli di altezza pagina intera.' >&2
    exit 1
else
    printf '%s\n' 'Nessun vincolo di altezza pagina intera residuo.'
fi

printf '\n%s\n' '===== COMPILAZIONE PDF ====='

mkdir -p "$OUTPUT_DIR"

# Evita di confondere un PDF parziale precedente con il nuovo risultato.
rm -f "$OUTPUT" "$OUTPUT_NAME"

run_xelatex() {
    xelatex -interaction=nonstopmode -halt-on-error \
        -file-line-error "$TEX_OUTPUT"
}

# Due passate bastano per indice e riferimenti; con un documento lungo e molte
# tabelle (xltabular) ne possono servire altre: massimo 4 in totale.
run_xelatex
run_xelatex

for _ in 1 2; do
    if grep -qE 'Rerun to get|Label\(s\) may have changed|Table widths have changed' \
            "${TEX_OUTPUT%.tex}.log"; then
        printf '%s\n' 'Riferimenti o larghezze tabella cambiati: passata aggiuntiva.'
        run_xelatex
    else
        break
    fi
done

if [[ -f "$OUTPUT_NAME" ]]; then
    mv -f "$OUTPUT_NAME" "$OUTPUT"
fi

printf '\n%s\n' '===== VERIFICA RISULTATO ====='
if [[ ! -s "$OUTPUT" ]]; then
    printf '%s\n' 'Errore: PDF assente o vuoto.' >&2
    exit 1
fi

if command -v pdfinfo >/dev/null 2>&1; then
    pdfinfo "$OUTPUT" | grep -E '^(Pages|Page size|File size):'
fi

if command -v qpdf >/dev/null 2>&1; then
    qpdf --check "$OUTPUT"
fi

printf '\n%s\n' '===== PULIZIA ARTEFATTI E STAGING ====='
rm -f "${TEX_OUTPUT%.tex}.aux" \
      "${TEX_OUTPUT%.tex}.log" \
      "${TEX_OUTPUT%.tex}.toc" \
      "${TEX_OUTPUT%.tex}.out" \
      "$TEX_OUTPUT"
rm -rf "$PDF_ASSETS_DIR" "$PDF_ASSETS_MAP"
printf 'Rimossi: %s.{aux,log,toc,out}, %s, %s/, %s\n' \
    "${TEX_OUTPUT%.tex}" "$TEX_OUTPUT" "$PDF_ASSETS_DIR" "$PDF_ASSETS_MAP"

printf '\nPDF generato correttamente: %s/%s\n' "$PWD" "$OUTPUT"

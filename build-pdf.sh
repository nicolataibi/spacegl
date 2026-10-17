
#!/usr/bin/env bash
set -Eeuo pipefail

cd "$(dirname "$0")"

INPUT="README.md"
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

for file in "$INPUT" cover.tex pdf-style.tex html-assets.lua \
            prepare-pdf-assets.py emoji-fallback.tex; do
    [[ -f "$file" ]] || {
        printf 'Errore: file mancante: %s\n' "$file" >&2
        exit 1
    }
done

printf '%s\n' '===== PREPARAZIONE ASSET ====='
python3 prepare-pdf-assets.py

printf '\n%s\n' '===== PREPARAZIONE README PER PDF ====='

python3 - "$INPUT" "$PDF_INPUT" <<'PY'
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
lines = source.splitlines()

labels = {
    "Version": "**Versione:** 3.0",
    "License": "**Licenza:** GPL 3.0",
    "Language": "**Linguaggio:** C",
    "Graphics": "**Grafica:** Vulkan / OpenGL",
    "Platform": "**Piattaforma:** Fedora 44",
    "Status": "**Stato:** Verified HMAC-SHA256",
    "Alliance": "**Flotta:** Stellar Alliance",
    "Security": "**Cifratura:** AES / ChaCha / Blowfish",
}

result = []
badge_group = []

def flush_badges():
    if badge_group:
        result.append("  ·  ".join(badge_group))
        badge_group.clear()

for line in lines:
    if "img.shields.io/badge/" in line:
        match = re.search(r"!\[([^\]]+)\]", line)
        name = match.group(1) if match else ""
        label = labels.get(name)

        if label:
            badge_group.append(label)
        else:
            flush_badges()
            result.append(line)
        continue

    flush_badges()
    result.append(line)

flush_badges()

Path(sys.argv[2]).write_text(
    "\n".join(result) + "\n",
    encoding="utf-8"
)

print("README originale preservato.")
print("Badge remoti convertiti in etichette testuali per il PDF.")
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
  -V title="SPACE GL — README" \
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

xelatex -interaction=nonstopmode -halt-on-error \
    -file-line-error "$TEX_OUTPUT"

xelatex -interaction=nonstopmode -halt-on-error \
    -file-line-error "$TEX_OUTPUT"

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

from pathlib import Path
import shutil

root = Path(__file__).resolve().parent
source = root / "readme_assets"
destination = root / "pdf-assets"
mapping = root / "pdf-assets-map.tsv"

extensions = {
    ".png", ".jpg", ".jpeg", ".webp", ".gif",
    ".bmp", ".tif", ".tiff", ".pdf"
}

if not source.is_dir():
    raise SystemExit(f"Cartella degli asset non trovata: {source}")

if destination.exists():
    shutil.rmtree(destination)

destination.mkdir(parents=True)

entries = sorted(
    p for p in source.rglob("*")
    if p.is_file() and p.suffix.lower() in extensions and p.name != "README.pdf"
)

with mapping.open("w", encoding="utf-8", newline="\n") as out:
    for number, original in enumerate(entries, start=1):
        relative = original.relative_to(root).as_posix()
        alias = f"pdf-assets/img_{number:05d}{original.suffix.lower()}"

        shutil.copy2(original, root / alias)
        out.write(f"{relative}\t{alias}\n")

print(f"Asset preparati: {len(entries)}")
print(f"Mappatura: {mapping.name}")

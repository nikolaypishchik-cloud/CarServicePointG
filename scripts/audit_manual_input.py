"""Inventory desktop manual input entry points without opening working databases."""
import csv
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
PATTERN = re.compile(
    r"new\s+(?:QLineEdit|QTextEdit|QPlainTextEdit|QDateEdit|QDateTimeEdit|WordStyleInlineEditor)\b"
    r"|setEditable\(true\)|createEditor\("
    r"|QInputDialog::get(?:Text|MultiLineText)\("
    r"|\b(?:makeEdit|makeCombo|addLine|addGridLine|addText)\("
)


def main():
    rows = []
    for path in sorted((ROOT / "src" / "ui").rglob("*")):
        if path.suffix not in {".cpp", ".h", ".ipp"}:
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
            if PATTERN.search(line):
                rows.append((path.relative_to(ROOT).as_posix(), number, line.strip()))
    output = ROOT / "docs" / "MANUAL_INPUT_SITES_2026_10_28.csv"
    with output.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("source", "line", "creation_or_factory_call"))
        writer.writerows(rows)
    print(f"{len(rows)} source sites in {len(set(r[0] for r in rows))} files: {output}")


if __name__ == "__main__":
    main()

"""Validate the PDF emitted by ReportsOrdersTests (requires pypdf)."""

import re
import sys
from decimal import Decimal, ROUND_HALF_UP

from pypdf import PdfReader


def number(text):
    return Decimal(re.sub(r"\s+", "", text))


reader = PdfReader(sys.argv[1])
text = "\n".join(page.extract_text(extraction_mode="layout") for page in reader.pages)
lines = text.splitlines()
assert "Сума\tпродажу" in text or "Сума продажу" in text, "Missing sale amount column"

expected = {
    "4L0698151H": ("1", "1250", "1250"),
    "Oil 5W40": ("8", "400", "3200"),
    "DEX 3": ("5", "350", "1750"),
    "Steering rod": ("2", "848", "1696"),
    "Fractional quantity": ("1.5", "80.5", "120.75"),
    "Rounded amount": ("1.25", "100.01", "125.01"),
}
total = Decimal(0)
for label, values in expected.items():
    matches = [line for line in lines if label in line.replace("\t", " ")]
    assert len(matches) == 1, (label, matches)
    fields = re.split(r" {2,}", matches[0].strip())
    qty, _, price, amount, _ = fields[-5:]
    actual = (number(qty), number(price), number(amount))
    exact = tuple(map(Decimal, values))
    displayed = (exact[0], exact[1].quantize(Decimal("1"), rounding=ROUND_HALF_UP),
                 exact[2].quantize(Decimal("1"), rounding=ROUND_HALF_UP))
    assert actual == displayed, (label, actual, displayed)
    assert exact[2] == (exact[0] * exact[1]).quantize(Decimal("0.01"), rounding=ROUND_HALF_UP)
    total += exact[2]

summary = next(line for line in lines if line.strip().startswith("Продаж"))
assert total == Decimal("8141.76")
assert number(summary.strip().removeprefix("Продаж")) == total.quantize(Decimal("1"), rounding=ROUND_HALF_UP)
print(f"PASS: {len(expected)} rows; sale total {total}; {len(reader.pages)} page(s)")

"""Check geometry of ReportsOrdersTests recommendation fixtures (pdfplumber)."""
import sys
from pathlib import Path
import pdfplumber

root = Path(sys.argv[1])
mm = 72 / 25.4
with pdfplumber.open(root / 'customer-recommendations.pdf') as pdf:
    page = pdf.pages[0]
    assert abs(min(r['top'] for r in page.rects) - 7 * mm) < 0.3
    summary, recommendations = page.rects[-2:]
    assert recommendations['width'] > summary['width'] * 2
    assert abs(recommendations['top'] - summary['bottom'] - 3 * mm) < 0.3
    assert recommendations['bottom'] <= page.height - 7 * mm

with pdfplumber.open(root / 'customer-long.pdf') as pdf:
    assert len(pdf.pages) > 1
    for page in pdf.pages:
        assert abs(min(r['top'] for r in page.rects) - 7 * mm) < 0.3
        box = page.rects[-1]
        assert box['width'] > 400
        assert box['bottom'] <= page.height - 7 * mm
        inside = [c for c in page.chars if box['top'] <= c['top'] and c['bottom'] <= box['bottom']]
        assert len(inside) > 20, 'Empty recommendation continuation'
print('PASS: 7 mm page limits, 3 mm summary gap, framed multipage recommendations')

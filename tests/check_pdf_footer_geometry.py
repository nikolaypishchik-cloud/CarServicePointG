"""Validate actual ReportsOrdersTests PDF footer fixtures; missing files fail."""
import sys
from pathlib import Path


def check_words(words):
    # Separately drawn text can be interleaved by the extractor. Compare boxes
    # instead of trusting extracted reading order or matching a whole sentence.
    errors = []
    for i, first in enumerate(words):
        for second in words[i + 1:]:
            dx = min(first['x1'], second['x1']) - max(first['x0'], second['x0'])
            dy = min(first['bottom'], second['bottom']) - max(first['top'], second['top'])
            if dx > 1 and dy > 1:
                errors.append((first['text'], second['text']))
    return errors


def main(root):
    import pdfplumber
    for name, minimum_markers in [('customer-footer.pdf', 1), ('customer-long-footer.pdf', 150)]:
        with pdfplumber.open(root / name) as pdf:
            markers = 0
            for page in pdf.pages:
                words = page.extract_words(use_text_flow=True)
                assert not check_words(words), (name, page.page_number, check_words(words))
                markers += sum(w['text'] == 'FooterStart' for w in words)
                for word in words:
                    assert 0 <= word['top'] < word['bottom'] <= page.height, (name, word)
            assert markers == minimum_markers, (name, 'clipped/missing requisites', markers)
            if minimum_markers > 1:
                assert len(pdf.pages) > 1
    print('PASS: footer word bounds, no text overlap, all requisites preserved')


if __name__ == '__main__':
    main(Path(sys.argv[1]))

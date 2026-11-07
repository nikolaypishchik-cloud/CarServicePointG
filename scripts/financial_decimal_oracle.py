"""Independent audit oracle: Decimal, no application imports or database writes."""
import json
from decimal import Decimal as D, ROUND_HALF_UP
from pathlib import Path
import argparse

def money(value):
    return value.quantize(D('0.01'), rounding=ROUND_HALF_UP)

def cents(value):
    return int(money(value) * 100)

def generate():
    rows = []
    for name, qty, cost, markup in [
        ('invoice_298_50', '1', '298.50', '0'),
        ('fractional_2_5', '2.5', '19.99', '20'),
        ('kopecks_three', '3', '0.33', '0'),
        ('half_kopeck', '1', '1.005', '0'),
        ('zero_cost', '2', '0', '20'),
        ('integer_control', '2', '100', '25'),
        ('large_amount', '9999', '99999.99', '10'),
    ]:
        # D01: retain unit precision, round each extended line before summing.
        q, p, m = D(qty), D(cost), D(markup)
        client = p * (1 + m / 100)
        rows.append(dict(id=name, qty=qty, cost=cost, markup=markup,
                         purchase_price_decimal=str(p), client_price_decimal=str(client),
                         purchase_price=cents(p), purchase_amount=cents(q*p),
                         client_price=cents(client), client_amount=cents(q*client)))
    return dict(rule='D01: Decimal ROUND_HALF_UP on each line; no intermediate unit-price rounding', rows=rows)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_text(json.dumps(generate(), indent=2), encoding='utf-8')

"""Reproduce frontend gains from pre-correction and post-correction readings.

Usage: python3 tools/fit_frontend_response.py [--check]
Follow-up readings include the first firmware's compensation. Frozen prior
multipliers are measurement metadata, not multipliers to apply a second time.
Range endpoints encode reported ranges, not independent measured trials.
Backsubstitution does not establish hardware accuracy or phase calibration.
"""
import argparse
import csv
import json
import math
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true', help='check compiled gains against source fixtures')
args = parser.parse_args()
groups = {}
with (root / 'tests/fixtures/frontend_response.csv').open(newline='') as source:
    for row in csv.DictReader(source):
        frequency = int(row['frequency_hz'])
        nominal, displayed = float(row['nominal_mVpp']), float(row['displayed_mVpp'])
        if not all(math.isfinite(v) and v > 0 for v in [nominal, displayed]):
            raise ValueError('Expected finite, positive bench voltages')
        groups.setdefault(frequency, []).append((nominal, displayed))
nodes = {}
for frequency, pairs in sorted(groups.items()):
    gain = sum(x*y for x,y in pairs) / sum(y*y for x,y in pairs)
    nodes[frequency] = {'frequency_hz': frequency, 'gain': gain, 'source': 'pre-correction fit',
                        'points': len(pairs),
                        'max_backsubstitution_error_mVpp': max(abs(gain*y-x) for x,y in pairs)}
with (root / 'tests/fixtures/frontend_response_followup.csv').open(newline='') as source:
    followup = list(csv.DictReader(source))
for row in followup:
    low, high = int(row['frequency_min_hz']), int(row['frequency_max_hz'])
    nominal, displayed = float(row['nominal_mVpp']), float(row['displayed_mVpp'])
    prior = [float(row['prior_gain_at_min']), float(row['prior_gain_at_max'])]
    if low > high or not all(math.isfinite(v) and v > 0 for v in [nominal, displayed, *prior]):
        raise ValueError('Invalid follow-up range, voltage or prior gain')
    # Only refine the observed local excess; other rows are validation controls.
    if not (100000 < low <= high <= 145000):
        continue
    for frequency, prior_gain in zip([low, high], prior):
        nodes[frequency] = {'frequency_hz': frequency,
                            'gain': prior_gain * nominal / displayed,
                            'source': 'post-correction range endpoint' if low != high else 'post-correction point',
                            'nominal_mVpp': nominal, 'prior_displayed_mVpp': displayed}
nodes = [nodes[f] for f in sorted(nodes)]
if args.check:
    text = (root / 'Algorithm/Private/frontend_response.h').read_text()
    compiled = [(int(float(f)), float(g)) for f,g in re.findall(r'\{\s*([0-9.]+)f,\s*([0-9.]+)f\s*\}', text)]
    expected = [(n['frequency_hz'], n['gain']) for n in nodes]
    if len(compiled) != len(expected) or any(f != ef or not math.isclose(g, eg, rel_tol=0, abs_tol=5e-8)
                                            for (f,g),(ef,eg) in zip(compiled,expected)):
        raise SystemExit('Compiled frontend gain table does not match the fixtures')
print(json.dumps({'original_points': sum(len(pairs) for pairs in groups.values()),
                  'followup_observations': len(followup), 'nodes': nodes,
                  'compiled_table_checked': args.check}, indent=2))

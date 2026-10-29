#!/usr/bin/env python3
"""Check maintained documentation links, example inventory, and API coverage."""
from pathlib import Path
import re
import sys
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
docs = [ROOT / 'README.md', ROOT / 'assets/samples/ATTRIBUTION.md',
        *sorted((ROOT / 'docs').glob('*.md'))]
errors = []
for doc in docs:
    text = doc.read_text()
    if len(re.findall(r'^```', text, re.M)) % 2:
        errors.append(f'{doc.relative_to(ROOT)}: unclosed code fence')
    for target in re.findall(r'(?<!!)\[[^\]]+\]\(([^)]+)\)', text):
        target = target.strip('<>')
        if re.match(r'[a-zA-Z][\w+.-]*:', target):
            continue
        path, _, anchor = unquote(target).partition('#')
        dest = (doc.parent / path).resolve() if path else doc
        if not dest.exists():
            errors.append(f'{doc.relative_to(ROOT)}: missing link {target}')
        elif anchor and dest.suffix == '.md':
            headings = re.findall(r'^#+\s+(.+)$', dest.read_text(), re.M)
            ids = [re.sub(r'[^\w\- ]', '', h.lower()).replace(' ', '-') for h in headings]
            if anchor not in ids:
                errors.append(f'{doc.relative_to(ROOT)}: missing anchor {target}')
    for name in re.findall(r'(?<![\w/])scripts/([\w.-]+)', text):
        if not (ROOT / 'scripts' / name).is_file():
            errors.append(f'{doc.relative_to(ROOT)}: missing script {name}')

combined = '\n'.join(p.read_text() for p in docs)
bindings = re.findall(r'bind\(candidate.ctx,"([^"]+)"', (ROOT / 'src/main.cpp').read_text())
for name in bindings:
    if not re.search(r'\b' + re.escape(name) + r'\b', combined):
        errors.append(f'undocumented global: {name}')
example_doc = ROOT / 'docs/examples.md'
example_inventory = example_doc.read_text()
# Every supported example belongs to one uninterrupted two-column GFM table.
# The guide is the source of truth for GitHub and the documentation site.
header = '| Script | Demonstrates |'
if header not in example_inventory:
    errors.append('docs/examples.md: missing examples table')
else:
    table = example_inventory.split(header, 1)[1].strip().splitlines()
    for row in table:
        if not re.fullmatch(r'\|[^|\n]+\|[^|\n]+\|', row):
            errors.append('docs/examples.md: interrupted or malformed examples table')
            break
for script in sorted((ROOT / 'scripts').glob('*')):
    if script.suffix != '.js':
        errors.append(f'non-example file in scripts/: {script.name}')
    else:
        if f'(../scripts/{script.name})' not in example_inventory:
            errors.append(f'example missing from docs/examples.md: {script.name}')
        for asset in re.findall(r'[\"\'](assets/[^\"\']+)[\"\']', script.read_text()):
            if not (ROOT / asset).is_file():
                errors.append(f'{script.relative_to(ROOT)}: missing asset {asset}')
for error in errors:
    print(error, file=sys.stderr)
if errors:
    sys.exit(1)
print(f'PASS: {len(docs)} documents, {len(bindings)} globals, '
      f'{len(list((ROOT / "scripts").glob("*.js")))} examples')

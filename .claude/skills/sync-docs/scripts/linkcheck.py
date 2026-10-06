"""Check relative markdown links and #anchors across the repository.

Usage: python linkcheck.py <repo root> [--all]
Archived docs are skipped as sources (they are frozen) unless --all.
"""
import os
import re
import sys
import unicodedata

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
ROOT = os.path.abspath(sys.argv[1])
CHECK_ARCHIVE = '--all' in sys.argv
SKIP_DIRS = {'node_modules', '.sst', 'build', '.git', '.claude', 'Drivers', 'Middlewares',
             'External', 'ST67W6X_Network_Driver', '.github'}

LINK = re.compile(r'(?<!\!)\[[^\]]*\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
HEADING = re.compile(r'^(#{1,6})\s+(.*?)\s*#*\s*$')


def slug(text):
    text = re.sub(r'`', '', text)
    text = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', text)  # links -> text
    text = text.strip().lower()
    out = []
    for ch in text:
        cat = unicodedata.category(ch)
        if ch in '-_ ' or cat[0] in 'LN':
            out.append('-' if ch == ' ' else ch)
    return ''.join(out)


_anchors = {}


def anchors(path):
    if path not in _anchors:
        seen = {}
        result = set()
        in_code = False
        with open(path, encoding='utf-8', errors='ignore') as f:
            for line in f:
                if line.lstrip().startswith('```'):
                    in_code = not in_code
                    continue
                if in_code:
                    continue
                m = HEADING.match(line)
                if m:
                    s = slug(m.group(2))
                    n = seen.get(s, 0)
                    seen[s] = n + 1
                    result.add(s if n == 0 else f'{s}-{n}')
                for a in re.findall(r'<a\s+(?:name|id)="([^"]+)"', line):
                    result.add(a)
        _anchors[path] = result
    return _anchors[path]


def sources():
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        rel = os.path.relpath(dirpath, ROOT).replace('\\', '/')
        if not CHECK_ARCHIVE and '/archive' in '/' + rel.lower():
            continue
        for name in filenames:
            if name.lower().endswith('.md'):
                yield os.path.join(dirpath, name)


bad = 0
for src in sorted(sources()):
    in_code = False
    with open(src, encoding='utf-8', errors='ignore') as f:
        lines = f.readlines()
    for number, line in enumerate(lines, 1):
        if line.lstrip().startswith('```'):
            in_code = not in_code
            continue
        if in_code:
            continue
        for target in LINK.findall(re.sub(r'`[^`]*`', '', line)):
            if re.match(r'^[a-z]+:', target) or target.startswith('//'):
                continue
            path, _, anchor = target.partition('#')
            dest = os.path.normpath(os.path.join(os.path.dirname(src), path)) if path else src
            problem = None
            if not os.path.exists(dest):
                problem = 'missing file'
            elif anchor and os.path.isfile(dest) and dest.lower().endswith('.md'):
                if anchor.lower() not in anchors(dest):
                    problem = 'missing anchor'
            if problem:
                bad += 1
                print(f'{os.path.relpath(src, ROOT)}:{number}: {problem}: {target}')
print(f'{bad} broken')

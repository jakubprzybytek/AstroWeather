"""Structural audit of the AstroWeather docs. Prints findings; exit code 1 if any.

Usage: python doc_audit.py <repo root> [--history]

Checks:
  layout     docs live in Docs/ folders (firmware/<project>/Docs, firmware/Docs,
             KiCad/Docs); no lowercase docs/ folder; no stray .md elsewhere
  archive    every archived doc starts with the "> Archived ..." header line and
             is listed in its folder's archive/README.md when one exists
  readme     each firmware project README has the required sections, and every
             feature-table row links a document (or says why not)
  code-refs  doc paths cited in code comments, tests, tools and help text exist
  history    (--history) lines in current docs that read like history or plans,
             for a human to judge; never a failure on their own
"""
import os
import re
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
ROOT = os.path.abspath(sys.argv[1])
SHOW_HISTORY = '--history' in sys.argv
SKIP = {'node_modules', '.sst', 'build', '.git', '.claude', 'Drivers', 'Middlewares', 'External',
        'ST67W6X_Network_Driver', 'sst', '.github', '.vscode', 'AstroWeather-backups'}
PROJECTS = ['HostControllerA', 'DisplayController', 'Bypass']
README_SECTIONS = {
    'HostControllerA': ['## Features', '## Known Limitations and Open Items', '## Documentation'],
    'DisplayController': ['## Features', '## Known Limitations and Open Items', '## Documentation'],
}
ARCHIVE_HEADER = re.compile(r'^> Archived \d{4}-\d{2}-\d{2}\. Current state: \[')
HISTORY = re.compile(r'\b(until 20\d\d|used to|previously|no longer|turned out|root cause|investigat\w*|'
                     r'withdrawn|was fixed|before this|phase \d|implementation plan|todo)\b', re.I)
DATE = re.compile(r'\b20\d\d-\d\d-\d\d\b')

findings = []


def rel(path):
    return os.path.relpath(path, ROOT).replace('\\', '/')


def walk(top):
    for dirpath, dirnames, filenames in os.walk(top):
        dirnames[:] = [d for d in dirnames if d not in SKIP]
        yield dirpath, dirnames, filenames


# layout
for top in ('firmware', 'KiCad'):
    for dirpath, dirnames, filenames in walk(os.path.join(ROOT, top)):
        for d in dirnames:
            if d == 'docs':
                findings.append(('layout', f'{rel(os.path.join(dirpath, d))}: rename to Docs'))
        r = rel(dirpath)
        in_docs = '/Docs' in '/' + r or r.endswith('Docs')
        for name in filenames:
            if not name.lower().endswith('.md'):
                continue
            if name == 'README.md' or in_docs:
                continue
            findings.append(('layout', f'{rel(os.path.join(dirpath, name))}: outside a Docs folder'))

# archive
for top in ('firmware', 'KiCad'):
    for dirpath, dirnames, filenames in walk(os.path.join(ROOT, top)):
        if os.path.basename(dirpath).lower() != 'archive':
            continue
        index = os.path.join(dirpath, 'README.md')
        index_text = open(index, encoding='utf-8').read() if os.path.exists(index) else None
        for name in sorted(filenames):
            if not name.lower().endswith('.md') or name == 'README.md':
                continue
            path = os.path.join(dirpath, name)
            lines = open(path, encoding='utf-8', errors='ignore').read().split('\n')
            head = [l for l in lines[:6] if l.strip()]
            if not any(ARCHIVE_HEADER.match(l) for l in head):
                findings.append(('archive', f'{rel(path)}: missing "> Archived <date>. Current state: [...]" header'))
            if index_text is not None and name not in index_text:
                findings.append(('archive', f'{rel(path)}: not listed in {rel(index)}'))

# readme
for project, sections in README_SECTIONS.items():
    path = os.path.join(ROOT, 'firmware', project, 'README.md')
    text = open(path, encoding='utf-8').read()
    for s in sections:
        if s not in text:
            findings.append(('readme', f'{rel(path)}: missing section "{s}"'))
    m = re.search(r'^## Features\n(.*?)(?=^## )', text, re.S | re.M)
    if m:
        for line in m.group(1).split('\n'):
            cells = [c.strip() for c in line.strip().strip('|').split('|')]
            if len(cells) < 4 or cells[0].startswith('**') or set(cells[0]) <= set('- ') or cells[0] == 'Feature':
                continue
            if '](' not in cells[-1] and '](' not in cells[-2]:
                findings.append(('readme', f'{rel(path)}: feature without a document link: {cells[0][:70]}'))
for project in PROJECTS + ['Common']:
    if not os.path.exists(os.path.join(ROOT, 'firmware', project, 'README.md')):
        findings.append(('readme', f'firmware/{project}: no README.md'))

# code-refs: doc paths named in code, tests, tools and help strings
CODE_EXT = ('.c', '.cpp', '.h', '.hpp', '.py', '.ps1', '.ld', '.txt', '.cmake', '.sh')
REF = re.compile(r'((?:firmware/|\.\./)?(?:[A-Za-z]+/)*[Dd]ocs/[A-Za-z0-9_/.-]+?\.md)')
for project in ['Common', 'HostControllerA', 'DisplayController', 'Bypass']:
    base = os.path.join(ROOT, 'firmware', project)
    for dirpath, dirnames, filenames in walk(base):
        for name in filenames:
            if not name.endswith(CODE_EXT) and name != 'CMakeLists.txt':
                continue
            path = os.path.join(dirpath, name)
            try:
                text = open(path, encoding='utf-8', errors='ignore').read()
            except OSError:
                continue
            for number, line in enumerate(text.split('\n'), 1):
                for ref in REF.findall(line):
                    candidates = [os.path.join(ROOT, ref), os.path.join(base, ref),
                                  os.path.join(ROOT, 'firmware', ref)]
                    if not any(os.path.exists(c) for c in candidates):
                        findings.append(('code-refs', f'{rel(path)}:{number}: {ref} does not exist'))
                    elif '/docs/' in '/' + ref:
                        findings.append(('code-refs', f'{rel(path)}:{number}: {ref}: use Docs/'))

for kind, text in findings:
    print(f'[{kind}] {text}')

# history (advisory)
if SHOW_HISTORY:
    print('\n-- lines to judge: history or plans in current docs (dates in sample output are fine) --')
    for top in ('firmware', 'KiCad'):
        for dirpath, dirnames, filenames in walk(os.path.join(ROOT, top)):
            if 'archive' in rel(dirpath).lower().split('/'):
                continue
            for name in filenames:
                if not name.lower().endswith('.md'):
                    continue
                path = os.path.join(dirpath, name)
                in_code = False
                for number, line in enumerate(open(path, encoding='utf-8', errors='ignore'), 1):
                    if line.lstrip().startswith('```'):
                        in_code = not in_code
                        continue
                    if in_code:
                        continue
                    if HISTORY.search(line) or (DATE.search(line) and not line.lstrip().startswith(('|', '`'))):
                        print(f'{rel(path)}:{number}: {line.strip()[:140]}')

print(f'{len(findings)} findings')
sys.exit(1 if findings else 0)

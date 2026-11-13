"""Build the labos manual from the memorylab notebook.

Usage: python3.12 tools/mkmanual.py NOTEBOOK.ipynb MANUAL_DIR FONT.h OUT.txt

The output has one block per line: a tag, a space and the text.
  S id      section start (the menu opens the manual at the section of the selected experiment)
  H1 H2 H3  headings          P  paragraph       L  list item ("1. text" or "– text")
  Q         note              F  formula         C  line of code
  TH TR     table header and row, cells split by tabs
  V name    film of the section                  G  gap
Inside text, \\x01 and \\x02 start and end emphasis (bold in the notebook).
MANUAL_DIR holds the labos texts: cpuinfo.md replaces the notebook cell of the identification,
<id>.md goes after the notebook text of that experiment.
"""
import json
import re
import sys
from pathlib import Path

# Notebook description cells in order, with their experiment id and film
EXPERIMENTS = [
    ('rowconf', 'exp1'), ('list_vs_array', 'exp2'), ('prefetch', 'exp3'), ('memory_read_optimization', 'exp4'),
    ('cache_conflicts', 'exp5'), ('cache_performance', 'exp6'), ('sorting_algorithms', 'exp7'),
]
# The notebook speaks of its cells; labos has screens
PHRASES = {'(ячейка «Идентификация процессора»)': '(экран «Идентификация процессора»)'}
SUBSTITUTES = {'−': '-', '‑': '-', ' ': ' ', '’': "'", '“': '«', '”': '»', '•': '–', ' ': ' '}
MATH = [(r'\\cdot', '·'), (r'\\times', '×'), (r'\\log', 'log'), (r'\\le', '≤'), (r'\\ge', '≥'), (r'\\,', ' '),
        (r'\^\{?2\}?', '²'), (r'\^\{?3\}?', '³'), (r'[{}]', '')]


def math_text(tex):
    for pattern, repl in MATH:
        tex = re.sub(pattern, repl, tex)
    return re.sub(r'\s+', ' ', tex).strip()


def inline(text):
    text = re.sub(r'\$([^$]+)\$', lambda m: math_text(m.group(1)), text)
    text = re.sub(r'\*\*(.+?)\*\*', '\x01\\1\x02', text)
    text = re.sub(r'(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])', r'\1', text)
    text = re.sub(r'`([^`]+)`', r'\1', text)
    return text.replace('\\_', '_').strip()


def is_note(paragraph):
    return paragraph.startswith('**Примечание к реализации.**')


def blocks(markdown):
    """Markdown of one cell to manual lines."""
    out, para, quote, code = [], [], [], None
    table_rows = []

    def flush():
        nonlocal para, quote
        if para:
            out.append('P ' + inline(' '.join(para)))
        if quote and not is_note(' '.join(quote)):
            out.append('Q ' + inline(' '.join(quote)))
        para, quote = [], []

    def flush_table():
        nonlocal table_rows
        for i, row in enumerate(table_rows):
            out.append(('TH ' if i == 0 else 'TR ') + '\t'.join(inline(c) for c in row))
        table_rows = []

    for raw in markdown.splitlines():
        line = raw.rstrip()
        if code is not None:
            if line.strip().startswith('```'):
                out.extend('C ' + c.replace('\t', '    ') for c in code)
                code = None
            else:
                code.append(line)
            continue
        stripped = line.strip()
        if stripped.startswith('|'):
            flush()
            cells = [c.strip() for c in stripped.strip('|').split('|')]
            if not all(re.fullmatch(r':?-{3,}:?', c) for c in cells if c):
                table_rows.append(cells)
            continue
        flush_table()
        if stripped.startswith('```'):
            flush()
            code = []
        elif not stripped or stripped == '---':
            flush()
        elif m := re.match(r'(#{1,3})\s+(.*)', stripped):
            flush()
            out.append(f'H{len(m.group(1))} ' + inline(m.group(2)))
        elif m := re.fullmatch(r'\$\$(.+)\$\$', stripped):
            flush()
            out.append('F ' + math_text(m.group(1)))
        elif stripped.startswith('>'):
            if para:
                flush()
            quote.append(stripped.lstrip('> ').strip())
        elif m := re.match(r'[*-]\s+(.*)', stripped):
            flush()
            out.append('L – ' + inline(m.group(1)))
        elif m := re.match(r'(\d+)\.\s+(.*)', stripped):
            flush()
            out.append(f'L {m.group(1)}. ' + inline(m.group(2)))
        else:
            if quote:
                flush()
            para.append(stripped)
    flush()
    flush_table()
    return out


def font_codes(header):
    return {int(m.group(1)) for m in re.finditer(r'^\s*\{(\d+),', Path(header).read_text(encoding='utf-8'), re.M)}


def main():
    notebook, manual_dir, font_h, dst = sys.argv[1], Path(sys.argv[2]), sys.argv[3], Path(sys.argv[4])
    cells = [''.join(c['source']) for c in json.load(open(notebook, encoding='utf-8'))['cells']
             if c['cell_type'] == 'markdown']
    intro = next(c for c in cells if c.startswith('# Лабораторная работа'))
    described = [c for c in cells if c.startswith('## Описание эксперимента')]
    questions = next(c for c in cells if 'Контрольные вопросы' in c)
    if len(described) < len(EXPERIMENTS):
        sys.exit(f'{notebook}: {len(described)} experiment descriptions, {len(EXPERIMENTS)} expected')

    lines = ['S intro'] + blocks(intro)
    lines += ['G', 'S cpuinfo'] + blocks((manual_dir / 'cpuinfo.md').read_text(encoding='utf-8'))
    for (exp_id, film), cell in zip(EXPERIMENTS, described):
        lines += ['G', f'S {exp_id}'] + blocks(cell)
        extra = manual_dir / f'{exp_id}.md'
        if extra.exists():
            lines += blocks(extra.read_text(encoding='utf-8'))
        lines.append(f'V {film}')
    lines += ['G', 'S questions'] + blocks(questions)

    text = '\n'.join(lines) + '\n'
    for a, b in {**PHRASES, **SUBSTITUTES}.items():
        text = text.replace(a, b)
    known = font_codes(font_h) | {9, 10, 1, 2}
    missing = sorted({ch for ch in text if ord(ch) not in known})
    if missing:
        print('mkmanual: no glyph for', ' '.join(f'{ch!r}' for ch in missing), file=sys.stderr)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(text, encoding='utf-8')
    print(f'{dst}: {len(lines)} blocks, {len(text.encode())} bytes')


if __name__ == '__main__':
    main()

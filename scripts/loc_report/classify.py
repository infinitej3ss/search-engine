#!/usr/bin/env python3
"""
maps each tracked source file to a (section, component) bucket

run as a script to emit a tsv:  file<TAB>section<TAB>component<TAB>wc_loc
"""
import subprocess
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

RULES = [
    ('other_lang', 'other', [
        'CMakeLists.txt',
        'CMakePresets.json',
        'src/crawler/Makefile',
        'src/crawler/LinuxGetSsl/Makefile',
        'src/crawler/robots.txt/Makefile',
        'continue.sh',
        'init.sh',
        'rebuild_and_continue.sh',
    ]),
    ('other_lang', 'Front end', ['frontend/', 'server/html/']),

    ('other_cpp', 'Experiments', [
        'test_ranker.cpp',
        'test_load_blobs.cpp',
        'src/crawler/page_file_test.cpp',
    ]),
    ('other_cpp', 'Test cases', ['tests/']),
    ('other_cpp', 'main( ) and other glue', [
        'src/main.cpp',
        'src/build_index.cpp',
        'src/majestic_million_parser.cpp',
    ]),
    ('other_cpp', 'Templates and libraries', []),

    ('major', 'HTML Parser', [
        'src/crawler/HtmlParser.h',
        'src/crawler/HtmlTags.cpp',
        'src/crawler/HtmlTags.h',
    ]),
    ('major', 'Constraint solver', [
        'src/index/constraint_solver.cpp',
        'src/index/constraint_solver.h',
        'src/index/isr.cpp',
        'src/index/isr.h',
    ]),
    ('major', 'Index', ['src/index/']),
    ('major', 'Query language', ['src/query/']),
    ('major', 'Ranker', ['src/ranker/', 'config/weights.hpp']),
    ('major', 'Front end', ['src/engine/', 'server/']),
    ('major', 'Crawler', ['src/crawler/']),
]


def _match(path, pattern):
    if pattern.endswith('/'):
        return path.startswith(pattern)
    return path == pattern


def classify(path):
    """returns (section, component) or (None, None) if no rule matches."""
    for section, name, patterns in RULES:
        if any(_match(path, p) for p in patterns):
            return (section, name)
    return (None, None)


# files we count. anything tracked by git that doesn't match these extensions
# or basenames is silently ignored (e.g. .md, .txt, data)
CODE_EXTS = ('.cpp', '.hpp', '.h', '.html', '.css', '.js', '.sh')
CODE_BASENAMES = {'CMakeLists.txt', 'CMakePresets.json', 'Makefile'}

IGNORE_PREFIXES = (
    'scripts/loc_report/',
    'crawler_test_files/',
    'data/',
    'build/',
)


def is_code(path):
    if path.startswith(IGNORE_PREFIXES):
        return False
    if path.endswith(CODE_EXTS):
        return True
    return path.rsplit('/', 1)[-1] in CODE_BASENAMES


def _wc(path):
    out = subprocess.check_output(['wc', '-l', str(ROOT / path)]).decode().split()
    return int(out[0])


def _tracked_source_files():
    raw = subprocess.check_output(['git', 'ls-files'], cwd=ROOT).decode().strip().split('\n')
    return [f for f in raw if is_code(f)]


if __name__ == '__main__':
    print('file\tsection\tcomponent\tloc')
    unclassified = []
    for f in _tracked_source_files():
        section, comp = classify(f)
        if section is None:
            unclassified.append(f)
            continue
        print(f'{f}\t{section}\t{comp}\t{_wc(f)}')
    if unclassified:
        print(f'!! {len(unclassified)} unclassified files:', file=sys.stderr)
        for f in sorted(unclassified):
            print(f'   {f}', file=sys.stderr)
        sys.exit(1)

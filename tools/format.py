#!/usr/bin/env python3
"""Format C/C++ sources, or check the exact staged contents before a commit."""
import argparse
import pathlib
import shutil
import subprocess
import sys


def git(*args):
    return subprocess.check_output(['git', *args])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--check', action='store_true')
    mode.add_argument('--apply', action='store_true')
    mode.add_argument('--staged', action='store_true')
    args = parser.parse_args()
    root = pathlib.Path(git('rev-parse', '--show-toplevel').decode().strip())
    import os
    os.chdir(root)
    if args.staged:
        names = git('diff', '--cached', '--name-only', '--diff-filter=ACMR', '-z')
    else:
        names = git('ls-files', '--cached', '--others', '--exclude-standard', '-z')
    files = sorted({os.fsdecode(p) for p in names.split(b'\0') if p and not pathlib.Path(os.fsdecode(p)).parts[0] == 'third_party' and
                    pathlib.Path(os.fsdecode(p)).suffix in
                    {'.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx'}})
    if not files:
        return 0
    if not shutil.which('clang-format'):
        print('clang-format is required; install it and retry.', file=sys.stderr)
        return 1
    failed = False
    for name in files:
        original = git('show', ':' + name) if args.staged else pathlib.Path(name).read_bytes()
        formatted = subprocess.run(
            ['clang-format', '--style=file', '--assume-filename=' + name],
            input=original, stdout=subprocess.PIPE, check=True).stdout
        if original == formatted:
            continue
        if args.apply:
            pathlib.Path(name).write_bytes(formatted)
        else:
            print('Formatting required: ' + name, file=sys.stderr)
            failed = True
    if failed:
        print('Run python3 tools/format.py --apply, review, then git add the formatted files.',
              file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

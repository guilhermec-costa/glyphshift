"""Check the Git hook against staged bytes while preserving partial staging."""
import pathlib
import shutil
import subprocess
import sys
import tempfile

source = pathlib.Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='glyphshift-format-') as temporary:
    root = pathlib.Path(temporary)
    (root / 'tools').mkdir()
    (root / '.githooks').mkdir()
    for name in ['.clang-format', 'tools/format.py', '.githooks/pre-commit']:
        shutil.copy2(source / name, root / name)

    def git(*args):
        return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.DEVNULL)

    def hook():
        return subprocess.run(['git', '-C', str(root), 'hook', 'run', 'pre-commit'],
                              capture_output=True, text=True)

    git('init', '--quiet')
    git('config', 'core.hooksPath', '.githooks')
    target = root / 'file with spaces.cpp'
    unformatted = b'int main(){return 0;}\n'
    formatted = subprocess.run(['clang-format', '--style=file',
                                '--assume-filename=' + str(target)],
                               input=unformatted, capture_output=True, check=True).stdout
    assert formatted != unformatted
    target.write_bytes(formatted)
    git('add', target.name)
    target.write_bytes(unformatted)
    staged = git('show', ':' + target.name)
    assert hook().returncode == 0, 'Hook checked the worktree instead of the index'
    assert git('show', ':' + target.name) == staged and target.read_bytes() == unformatted
    # Reverse the partial staging: a formatted worktree must not hide bad staging.
    git('add', target.name)
    target.write_bytes(formatted)
    staged = git('show', ':' + target.name)
    result = hook()
    assert result.returncode != 0 and 'Formatting required' in result.stderr, result
    assert git('show', ':' + target.name) == staged and target.read_bytes() == formatted
    git('add', target.name)
    assert hook().returncode == 0
    git('-c', 'user.name=GlyphShift tests', '-c',
        'user.email=tests@example.invalid', '-c', 'commit.gpgsign=false',
        'commit', '--no-verify', '--quiet', '-m', 'Formatting test fixture')
    git('rm', '--cached', target.name)
    assert git('diff', '--cached', '--name-status').startswith(b'D\t')
    assert hook().returncode == 0, 'Deleted files must not be formatted'
print('Pre-commit: formatted staging accepted, unformatted staging refused; index/worktree preserved.')

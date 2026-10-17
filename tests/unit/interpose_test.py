"""Check forwarding, one-time initialization, recursion and unresolved symbols."""
import os
import subprocess
import sys

executable, module, fake = sys.argv[1:]
for missing, recurse, logging in [(False, False, False), (False, False, True),
                                  (False, True, True), (True, False, True),
                                  (True, False, False)]:
    env = os.environ.copy()
    for name in ['GLYPHSHIFT_LOG', 'GLYPHSHIFT_TEST_RECURSE', 'GLYPHSHIFT_TEST_MISSING']:
        env.pop(name, None)
    env['LD_PRELOAD'] = module if missing else module + ':' + fake
    if missing:
        env['GLYPHSHIFT_TEST_MISSING'] = '1'
    if recurse:
        env['GLYPHSHIFT_TEST_RECURSE'] = '1'
    if logging:
        env['GLYPHSHIFT_LOG'] = '1'
    result = subprocess.run([executable], env=env, capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result
    assert not result.stdout, result
    expected = []
    if logging:
        expected.append('glyphshift: cannot resolve next glXSwapBuffers; presentation skipped'
                        if missing else 'glyphshift: GLX pass-through initialized')
        if recurse:
            expected.append('glyphshift: recursive glXSwapBuffers suppressed')
    assert sorted(result.stderr.splitlines()) == sorted(expected), result
print('Interposer: 8 concurrent threads, 8000 forwards, recursion and missing symbol passed.')

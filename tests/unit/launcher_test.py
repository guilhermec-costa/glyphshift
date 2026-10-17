"""Exercise the real launcher without X11 or a GPU."""
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile

launcher, probe = map(pathlib.Path, sys.argv[1:])
module = launcher.parent / 'libglyphshift_gl.so'
original_preload = os.environ.get("LD_PRELOAD")
env = os.environ.copy()
env.pop('LD_PRELOAD', None)
env.pop('GLYPHSHIFT_LOG', None)
for name in ['VK_LAYER_PATH', 'VK_ADD_LAYER_PATH', 'VK_INSTANCE_LAYERS']:
    env.pop(name, None)


def run(*args, expected=0, environment=env, cwd=None):
    result = subprocess.run([str(launcher), *map(str, args)], env=environment,
                            cwd=cwd, capture_output=True, text=True)
    assert result.returncode == expected, (args, result)
    return result


run('--help')
run(expected=2)
run('--', expected=2)
run('bad-option', expected=2)
if os.getuid() == 0:
    assert 'privileged' in run('--', probe, expected=126).stderr
    print('Root invocation refused; normal-user launcher checks require a non-root test run.')
    sys.exit(0)

result = run('--', probe, 'two words', '--', '$literal', '', cwd='/tmp')
assert result.stdout.splitlines() == [f'LD_PRELOAD={module}', 'VK_LAYER_PATH=<unset>',
    f'VK_ADD_LAYER_PATH={launcher.parent}/layers',
    'VK_INSTANCE_LAYERS=VK_LAYER_GLYPHSHIFT_overlay', 'NO_NEW_PRIVS=1',
    f'ARG={probe}', 'ARG=two words', 'ARG=--', 'ARG=$literal', 'ARG=']
assert not result.stderr
run('--', probe, '--exit', expected=37)
run('--', probe, '--signal', expected=-signal.SIGTERM)
assert 'cannot find' in run('--', 'glyphshift-no-such-target-94715', expected=127).stderr
assert 'not executable' in run('--', '/etc/passwd', expected=126).stderr
run('--', '/tmp', expected=126)
path_env = dict(env, PATH=str(probe.parent))
assert f'ARG={probe.name}' in run('--', probe.name, environment=path_env).stdout
# Existing preloads are retained; use our own real module to avoid loader noise.
preloaded_env = dict(env, LD_PRELOAD=str(module))
assert f'LD_PRELOAD={module}:{module}' in run('--', probe, environment=preloaded_env).stdout
assert os.environ.get('LD_PRELOAD') == original_preload
# Discovery overrides and requested layers remain available to the child.
override = dict(env, VK_LAYER_PATH='/tmp/validation', VK_ADD_LAYER_PATH='/tmp/extra',
                VK_INSTANCE_LAYERS='VK_LAYER_test_existing')
result = run('--', probe, environment=override)
assert f'VK_LAYER_PATH={launcher.parent}/layers:/tmp/validation' in result.stdout
assert 'VK_ADD_LAYER_PATH=/tmp/extra' in result.stdout
assert 'VK_INSTANCE_LAYERS=VK_LAYER_GLYPHSHIFT_overlay:VK_LAYER_test_existing' in result.stdout
additional = dict(env, VK_ADD_LAYER_PATH='/tmp/extra')
assert f'VK_ADD_LAYER_PATH={launcher.parent}/layers:/tmp/extra' in run('--', probe, environment=additional).stdout

with tempfile.TemporaryDirectory(prefix='glyphshift-launcher-') as directory:
    target = pathlib.Path(directory) / 'target with spaces'
    shutil.copy2(probe, target)
    assert f'ARG={target}' in run('--', target).stdout
    for mode in (0o4755, 0o2755):
        target.chmod(mode)
        assert 'setuid/setgid' in run('--', target, expected=126).stderr
        link = target.parent / 'target-link'
        link.symlink_to(target)
        assert 'setuid/setgid' in run('--', link, expected=126).stderr
        link.unlink()
    target.chmod(0o755)
    target.write_text('#!/bin/sh\nexit 0\n')
    assert 'ELF target' in run('--', target, expected=126).stderr
    target.write_bytes(b'\x7fELF')
    run('--', target, expected=126)
    isolated = target.parent / 'glyphshift'
    shutil.copy2(launcher, isolated)
    missing = subprocess.run([str(isolated), '--', str(probe)], env=env,
                             capture_output=True, text=True)
    assert missing.returncode == 126 and 'libglyphshift_gl.so' in missing.stderr
    shutil.copy2(module, isolated.parent / module.name)
    missing = subprocess.run([str(isolated), '--', str(probe)], env=env,
                             capture_output=True, text=True)
    assert missing.returncode == 126 and 'libglyphshift_vk.so' in missing.stderr
    shutil.copy2(launcher.parent / 'libglyphshift_vk.so', isolated.parent / 'libglyphshift_vk.so')
    missing = subprocess.run([str(isolated), '--', str(probe)], env=env,
                             capture_output=True, text=True)
    assert missing.returncode == 126 and 'VK_LAYER_GLYPHSHIFT_overlay.json' in missing.stderr
print('Launcher: arguments, PATH, environment, exit/signals, privilege refusal and failures passed.')

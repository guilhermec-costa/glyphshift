"""Opt-in real X11/GPU smoke checks; no network or system installation."""
import argparse
import contextlib
import os
import pathlib
import subprocess
import sys
import time
from x11 import X11

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build',type=pathlib.Path,required=True)
parser.add_argument('--api',choices=['glx','vulkan'],required=True)
parser.add_argument('--output',type=pathlib.Path)
args=parser.parse_args();build=args.build.resolve();api=args.api
if not os.getenv('DISPLAY'):
    print('SKIP: DISPLAY is not set');sys.exit(77)
try:x=X11()
except RuntimeError as error:
    print('SKIP:',error);sys.exit(77)
output=(args.output or build/'smoke-evidence'/api).resolve();output.mkdir(parents=True,exist_ok=True)
# Seconds for a live config change or key to reach the screen; software renderers need more.
settle=float(os.environ.get('GLYPHSHIFT_SMOKE_SETTLE','0.25'))
env=os.environ.copy()
for name in ['LD_PRELOAD','GLYPHSHIFT_CONFIG','GLYPHSHIFT_PROFILE','GLYPHSHIFT_PRESET','GLYPHSHIFT_LOG','GLYPHSHIFT_FIXTURE_MULTI','GLYPHSHIFT_FIXTURE_SRGB']:
    env.pop(name,None)
env.update(XDG_CONFIG_HOME=str(output/'config'),GLYPHSHIFT_FIXTURE_NO_FOCUS='1',GLYPHSHIFT_FIXTURE_FLOATING='1',GLYPHSHIFT_FIXTURE_GL_DEBUG='1',GLYPHSHIFT_FIXTURE_STATE_CHECK='1',GLYPHSHIFT_FIXTURE_INPUT_LOG='1',VK_LAYER_VALIDATE_SYNC='1')
env['GLYPHSHIFT_FIXTURE_INPUT_MODE']='press-only' if api=='glx' else 'release-only'
env['GLYPHSHIFT_FIXTURE_PROC_SWAP']='1'
# NVIDIA's shader disk cache stops accepting programs once full (about 1 GB);
# a private cache keeps link times, and the bounded runs' timeouts, independent
# of the user's cache. Other drivers ignore the variable.
env['__GL_SHADER_DISK_CACHE_PATH']=str(build/'shader-cache')
profile=f'smoke-{api}-{os.getpid()}'
sequence=0
@contextlib.contextmanager
def launch(label,injected=True,extra=None,bounded=False):
    global sequence
    sequence+=1;suffix=f' {os.getpid()}-{sequence}'
    local=dict(env,GLYPHSHIFT_FIXTURE_TITLE_SUFFIX=suffix)
    if extra:local.update(extra)
    command=[str(build/f'glyphshift-{api}-fixture'),'--fixed-time','1.25','--frames','60' if bounded else '9000']
    if api=='vulkan':command+=['--validation']
    if injected:command=[str(build/'glyphshift'),'--profile',profile,'--',*command]
    log=output/f'{label}.log'
    with log.open('w') as stream:
        process=subprocess.Popen(command,env=local,stdout=stream,stderr=subprocess.STDOUT)
        try:
            windows=[];deadline=time.monotonic()+15
            title=f'GlyphShift {"GLX" if api=="glx" else "Vulkan"} fixture{suffix}'
            while time.monotonic()<deadline:
                windows=x.windows(title)
                if windows:break
                if process.poll() is not None:raise RuntimeError(log.read_text())
                time.sleep(.05)
            if not windows:raise RuntimeError('fixture did not create a window: '+log.read_text())
            yield process,windows[0],log
        finally:
            if process.poll() is None:process.terminate();process.wait(timeout=10)

def image(window,label):
    time.sleep(max(.15,settle))
    for attempt in range(4):
        try:picture=x.image(window);break
        except RuntimeError:
            if attempt==3:raise
            time.sleep(.15)
    w,h,rgb=picture;(output/f'{label}.ppm').write_bytes(f'P6\n{w} {h}\n255\n'.encode()+rgb)
    return picture

def finish(process,window,log,expected_failure=False):
    x.close(window)
    assert process.wait(timeout=15)==0,log.read_text()
    text=log.read_text()
    assert 'GL debug:' not in text and 'Vulkan validation:' not in text,text
    if not expected_failure:assert 'disabled' not in text and 'unsupported' not in text,text
    return text
try:
    with launch('direct',False) as (process,window,log):
        x.resize(window,800,600);original=image(window,'original');finish(process,window,log)
    with launch('injected') as (process,window,log):
        x.resize(window,800,600);glyph=image(window,'glyph');assert glyph!=original,'effect did not transform image'
        x.key(window,0xffc7);assert image(window,'off')==original,'disabled effect changed source'
        x.key(window,0xff63);assert image(window,'editor')!=original,'editor not drawn'
        x.key(window,0xff1b);assert image(window,'closed')==original,'Escape did not close only editor'
        x.key(window,ord('a'))
        # Exercise the strength widget at both ends and save a nondefault value.
        def strength(value):
            x.key(window,0xff63)
            x.send(window,2,0xffe3);time.sleep(.06)
            x.send(window,6,x=100,y=301,state=4)
            x.send(window,4,1,100,301,state=4);time.sleep(.06)
            x.send(window,5,1,100,301,state=4)
            x.send(window,3,0xffe3,state=4);time.sleep(.12)
            x.send(window,2,0xffe3);time.sleep(.06)
            x.send(window,2,ord('a'),state=4);x.send(window,3,ord('a'),state=4)
            x.send(window,3,0xffe3,state=4);time.sleep(.12)
            for letter in value:x.key(window,ord(letter))
            x.key(window,0xff0d);x.key(window,0xff1b)
        x.key(window,0xffc7)
        strength('0');assert image(window,'strength-zero')==original,'zero strength did not retain source pixels'
        strength('1');pure=image(window,'strength-one');assert pure!=original
        strength('0.37');glyph=image(window,'strength-fraction')
        assert glyph!=original and glyph!=pure,'fractional strength did not change rendering'
        # Select binary through the real editor, apply, then persist it.
        x.key(window,0xff63);x.click(window,100,330)
        x.click(window,100,359);x.click(window,100,456)
        x.click(window,100,524);x.click(window,100,330);x.key(window,0xff1b)
        selected=image(window,'selected-binary')
        assert selected!=glyph,'editor character selector did not change output'
        glyph=selected
        x.key(window,0xffc7)
        # Save the disabled effect under a named preset through the real UI.
        x.key(window,0xff63);x.click(window,90,122)
        x.send(window,2,0xffe3);time.sleep(.06);x.send(window,2,ord('a'),state=4);x.send(window,3,ord('a'),state=4);x.send(window,3,0xffe3,state=4);time.sleep(.12)
        for letter in 'night':x.key(window,ord(letter))
        x.key(window,0xff0d);x.click(window,288,122);image(window,'saved-preset')
        directory=output/'config'/'glyphshift'/'profiles'/profile
        assert (directory/'night.conf').is_file(),'Save preset did not create named file; inspect editor.ppm'
        assert 'enabled=false' in (directory/'night.conf').read_text()
        assert 'glyph_strength=0.37\n' in (directory/'night.conf').read_text()
        assert 'character_set=binary\n' in (directory/'night.conf').read_text()
        assert (directory/'active').read_text()=='night\n'
        x.key(window,0xff1b);text=finish(process,window,log)
        assert [line for line in text.splitlines() if line.startswith('Fixture input:')]==(['Fixture input: key down 97'] if api=='glx' else ['Fixture input: key up 97']),text
    with launch('restart') as (process,window,log):
        x.resize(window,800,600);assert image(window,'restarted-preset')==original,'preset not retained across restart'
        x.key(window,0xffc7);assert image(window,'re-enabled')==glyph
        for index,(w,h) in enumerate([(320,240),(1024,768),(640,480),(1000,300),(300,800),(800,600)]):
            x.resize(window,w,h);before=image(window,f'resize-{index}-before')
            x.key(window,0xff63);assert image(window,f'resize-{index}-editor')!=before
            x.key(window,0xff1b);assert image(window,f'resize-{index}-after')==before
        finish(process,window,log)
    # Change atlas/ramp live, including 256 Braille cells and font fallback.
    config=output/'characters.conf'
    base='enabled=true\ncolor_mode=mono\nglyph_strength=1\ncell_width=8\ncell_height=12\n'
    def characters(text):
        temp=config.with_suffix('.tmp');temp.write_text(base+text);temp.replace(config);time.sleep(settle)
    characters('character_set=classic\n')
    with launch('characters',extra={'GLYPHSHIFT_CONFIG':str(config),'GLYPHSHIFT_PRESET':'default','GLYPHSHIFT_LOG':'1'}) as (process,window,log):
        x.resize(window,800,600);classic=image(window,'characters-classic')
        pictures=[]
        for name,text in [
            ('ascii','character_set=ascii\n'),
            ('binary','character_set=binary\n'),
            ('blocks','character_set=blocks\n'),
            ('braille','character_set=braille\n'),
            ('custom','characters=" .01ABCé░▒▓█"\nglyph_order=density\n'),
            ('reverse','character_set=blocks\nglyph_reverse=true\n')]:
            characters(text);picture=image(window,'characters-'+name)
            assert picture!=classic,'character selection did not update: '+name
            pictures.append(picture)
        assert all(pictures[i]!=pictures[j] for i in range(len(pictures)) for j in range(i))
        characters('character_set=ascii\nglyph_font="/missing/glyphshift-font.ttf"\n')
        assert image(window,'characters-font-fallback')==classic,'missing font did not fall back safely'
        characters('character_set=classic\n')
        assert image(window,'characters-restored')==classic,'restoring the classic atlas changed its image'
        # Illustration controls are shared by both backends, with no atlas
        # rebuild for tone/color edits and dedicated slots for contour glyphs.
        art='render_style=illustrated\ncolor_mode=source\nedge_threshold=0.014\n'
        characters(art);illustrated=image(window,'illustrated-fill-contours')
        assert illustrated!=classic
        characters(art+'contour_overlay=false\n');replaced=image(window,'illustrated-replace-contours')
        assert replaced!=illustrated,'contour overlay option missing'
        characters(art+'edge_strength=0\n');filled=image(window,'illustrated-fill-only')
        assert filled!=illustrated,'contour contribution did not change image'
        characters(art+'edge_characters="████"\n')
        assert image(window,'illustrated-custom-contours')!=illustrated,'custom contours did not update'
        characters(art+'fill_enabled=false\n');outline=image(window,'illustrated-outline-only')
        assert outline!=illustrated and outline!=filled,'independent contour/fill controls missing'
        characters(art+'edge_strength=0\nfill_enabled=false\nbackground=0.2,0.3,0.4\n')
        paper=image(window,'illustrated-paper')[2]
        assert max(paper[0::3])-min(paper[0::3])==0 and all(abs(a-b)<=1 for a,b in zip(paper[:3],[51,77,102])),'background compositing incorrect'
        characters(art+'edge_strength=0\ntone_exposure=2\ntone_contrast=1.3\ntone_gamma=0.7\n')
        assert image(window,'illustrated-tone')!=filled,'tone controls missing'
        # A static scene must render identically with history: the previous
        # grid holds the same decisions, so ping-pong plumbing is exact.
        characters(art+'stability=1\n')
        assert image(window,'illustrated-stable')==illustrated,'temporal stability changed a static scene'
        characters(art+'edge_detector=dog\n')
        assert image(window,'illustrated-dog')!=illustrated,'DoG contours did not change image'
        characters(art+'crisp_glyphs=true\n')
        assert image(window,'illustrated-crisp')!=illustrated,'crisp glyphs did not change image'
        characters(art+'glow=0.8\n')
        assert image(window,'illustrated-glow')!=illustrated,'glow did not change image'
        # Metering pass: the fixture's mean is not the exposure key, so full
        # automatic exposure changes the image; a static scene stays put.
        characters(art+'auto_exposure=1\n');metered=image(window,'illustrated-auto-exposure')
        assert metered!=illustrated,'automatic exposure did not change image'
        assert image(window,'illustrated-auto-exposure-again')==metered,'automatic exposure drifted on a static scene'
        characters(art+'saturation=0\n')
        assert image(window,'illustrated-saturation')!=illustrated,'saturation did not change image'
        characters(art+'scene_paper=0.6\n');tiled=image(window,'illustrated-paper-tiles')
        characters(art+'scene_paper=0.6\npaper_wash=true\n')
        assert image(window,'illustrated-paper-wash')!=tiled,'paper wash did not change image'
        structure=art+'character_set=custom\ncharacters=" .:-+*#@█▘▝▀▖▌▞▛▗▚▐▜▄▙▟"\nscene_paper=0.9\n'
        characters(structure);tonal=image(window,'shape-tonal-only')
        characters(structure+'shape_matching=true\nshape_threshold=0.001\n')
        assert image(window,'shape-matched')!=tonal,'local shape matching did not update'
        characters(structure+'shape_matching=true\nshape_threshold=0.001\nglyph_strength=0\n')
        assert image(window,'shape-zero')==original,'shape matching must retain exact source at zero'
        characters(art+'glyph_strength=0\n')
        assert image(window,'illustrated-zero')==original,'zero strength must preserve source'
        characters(art+'cell_width=4\ncell_height=4\n')
        assert image(window,'illustrated-small-cells')!=illustrated,'small-cell sampling missing'
        characters('character_set=classic\n')
        assert image(window,'illustrated-restore-classic')==classic
        finish(process,window,log)
    # World-anchored grid on live frames: after one second the fixture's block
    # texture scrolls left one pixel per frame for N frames, then holds. With 7-pixel cells
    # the anchored grid is back on the screen grid after 21 pixels and three
    # pixels off it after 24, which exercises the motion search, resolve and
    # camera-state alternation of each backend.
    anchored_text=base.replace('cell_width=8','cell_width=7')+'render_style=illustrated\ncolor_mode=source\n'
    for steps,aligned in ((21,True),(24,False)):
        characters('');config.write_text(anchored_text+'anchor_grid=true\n')
        with launch(f'anchor-{steps}',extra={'GLYPHSHIFT_CONFIG':str(config),'GLYPHSHIFT_PRESET':'default','GLYPHSHIFT_FIXTURE_SCROLL':str(steps)}) as (process,window,log):
            x.resize(window,800,600);time.sleep(2)
            anchored=image(window,f'anchor-{steps}')
            temp=config.with_suffix('.tmp');temp.write_text(anchored_text);temp.replace(config);time.sleep(settle)
            screen=image(window,f'anchor-{steps}-screen-grid')
            assert (anchored==screen)==aligned,f'anchored grid after {steps} scrolled pixels: aligned={anchored==screen}'
            finish(process,window,log)
    # A fresh profile renders both contexts/swapchains, not the saved disabled effect.
    with launch('multi',extra={'GLYPHSHIFT_FIXTURE_MULTI':'1','GLYPHSHIFT_PRESET':'default'},bounded=True) as (process,window,log):
        assert process.wait(timeout=30)==0,log.read_text()
        text=log.read_text();assert ('two contexts' if api=='glx' else 'two swapchains / one device') in text,text
        assert 'GL debug:' not in text and 'Vulkan validation:' not in text,text
    if api=='glx':
        with launch('shader-failure',extra={'LD_PRELOAD':str(build/'libglyphshift-fault-glx.so'),'GLYPHSHIFT_PRESET':'default'}) as (process,window,log):
            x.resize(window,800,600);assert image(window,'failed-pass-through')==original
            x.key(window,ord('a'));text=finish(process,window,log,True)
            assert text.count('GL context disabled:')==1,text
    if api=='vulkan':
        with launch('unsupported-present',extra={'GLYPHSHIFT_FIXTURE_EXTENDED_PRESENT':'1','GLYPHSHIFT_PRESET':'default','GLYPHSHIFT_LOG':'1'}) as (process,window,log):
            x.resize(window,800,600);assert image(window,'unsupported-pass-through')==original
            x.close(window);assert process.wait(timeout=15)==0,log.read_text()
            text=log.read_text();assert text.count('unsupported Vulkan present batch/pNext')==1,text
            assert 'Vulkan validation:' not in text,text
    if api=='glx':
        subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('atlas_filter.py')),'--build',str(build),'--output',str(output/'atlas-filter')],check=True)
        subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('temporal.py')),'--build',str(build),'--output',str(output/'temporal')],check=True)
        subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('exposure.py')),'--build',str(build),'--output',str(output/'exposure')],check=True)
        subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('anchor.py')),'--build',str(build),'--output',str(output/'anchor')],check=True)
        subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('overlay.py')),'--build',str(build),'--output',str(output/'overlay')],check=True)
        subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('overlay_tray.py')),'--build',str(build),'--output',str(output/'overlay-tray')],check=True)
    print(f'{api}: launch, editor/input, live toggle, strength 0/1/fraction, named preset restart, character/Unicode/font switching, 6 resizes, multi-target lifetime'+(', shader failure pass-through' if api=='glx' else '')+' passed')
finally:x.shutdown()

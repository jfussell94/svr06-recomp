"""SvR06 Launcher 2 - every option the launcher shows, with plain-language descriptions.

One source of truth for the UI (labels, help text, badges) and the backend (validation, where a value is stored).
store='game'  -> svr06_settings.json of the selected version (shared with the in-game F1 menu; the game re-reads it live)
store='pref'  -> launcher prefs (per version), mirrored into svr06_settings.json by the old backend's sync
applies: 'live' = takes effect in the running game, 'restart' = next time the game starts
"""

def opt(v, label, hint=''):
    return {'v': v, 'label': label, 'hint': hint}

NEUTRAL_IMAGE = {'image.brightness': 0, 'image.contrast': 0, 'image.saturation': 100, 'image.vibrance': 0, 'image.gamma': 100,
                 'image.black': 0, 'image.temp': 0, 'image.hue': 0, 'image.sharp': 0}

S = []  # flat list of settings, in page order


def add(page, section, key, label, kind, desc, detail='', default=0, options=None, min=0, max=1, step=1, unit='', applies='live',
        tags=(), store='game', fmt=None):
    S.append({'page': page, 'section': section, 'key': key, 'label': label, 'kind': kind, 'desc': desc, 'detail': detail,
              'default': default, 'options': options or [], 'min': min, 'max': max, 'step': step, 'unit': unit,
              'applies': applies, 'tags': list(tags), 'store': store, 'fmt': fmt})


# ------------------------------------------------------------------ Graphics
G = 'graphics'
add(G, 'Display', 'graphics.resolution', 'Window resolution', 'select',
    'Size of the game window, and the screen mode used by exclusive fullscreen.',
    'This sets the window size. How detailed the 3D picture is rendered is set separately by Internal resolution below. '
    'In exclusive fullscreen your monitor switches to this resolution.',
    0, [opt(0, '640 x 448', 'Original PS2 size'), opt(1, '854 x 480', '480p'), opt(2, '1280 x 720', '720p HD'),
        opt(3, '1920 x 1080', '1080p Full HD'), opt(4, '2560 x 1440', '1440p QHD'), opt(5, '3840 x 2160', '4K UHD')], max=5)
add(G, 'Display', 'graphics.fullscreen', 'Fullscreen', 'toggle',
    'Start the game in fullscreen. F11 switches between window and fullscreen at any time.', default=0)
add(G, 'Display', 'graphics.fsMode', 'Fullscreen type', 'segmented',
    'How fullscreen covers your monitor.',
    'Borderless keeps your desktop resolution and is the safe choice. Exclusive switches the monitor to the window resolution above.', 0, [opt(0, 'Borderless', 'Desktop resolution'), opt(1, 'Exclusive', 'Monitor switches resolution')])
add(G, 'Display', 'graphics.aspect', 'Aspect ratio', 'segmented',
    'Shape of the picture.',
    'The game draws a 4:3 picture. 4:3 shows it exactly as on a PS2; the other choices widen it to fill the window and stretch the picture.',
    0, [opt(0, '4:3', 'Original'), opt(1, '16:9', 'Stretched'), opt(2, '21:9', 'Stretched'), opt(3, 'Stretch', 'Fill window')], max=3,
    tags=('recommended:0',))
add(G, 'Display', 'graphics.integer', 'Integer scaling', 'toggle',
    'Scale the picture by whole multiples only, for the sharpest pixels.',
    'Every game pixel becomes an exact block of screen pixels, so nothing blurs. May leave black borders when the window is not an exact multiple of the game size.')

add(G, 'Quality', 'graphics.internalRes', 'Internal resolution', 'cards',
    'How detailed the 3D scene is rendered on your graphics card.',
    'Native draws at the PS2 resolution and is the lightest on your GPU. Anti-aliasing (SSAA) renders extra samples per pixel to smooth jagged edges. 2x renders at double the resolution. Higher settings cost GPU time: if frames drop in busy scenes, step down one level. Textures stay at their original size unless the HD texture pack is on.',
    0, [opt(0, 'Native', 'Original look, fastest'), opt(1, 'Native + SSAA 4x', 'Smooth edges, light cost'),
        opt(2, '2x', '1280 x 896, sharper 3D'), opt(3, '2x + SSAA 8x', 'Sharpest, heaviest')], max=3, applies='restart',
    tags=('recommended:1', 'cost'))
add(G, 'Quality', 'graphics.filter', 'Screen filter', 'segmented',
    'How the finished picture is scaled up to your window.',
    'Nearest keeps every pixel crisp, like the PS2 on a sharp display. Bilinear blends neighbouring pixels for a softer image '
    'with no visible pixel edges.', 0, [opt(0, 'Nearest', 'Crisp pixels'), opt(1, 'Bilinear', 'Soft and smooth')])
add(G, 'Quality', 'graphics.msaa', 'Hardware anti-aliasing (MSAA)', 'slider',
    'Smooths jagged edges on 3D models using your GPU\'s multisampling.',
    'In development. 1x is off. Internal resolution anti-aliasing (SSAA) above is the tested way to smooth edges.', 1, min=1, max=8, unit='x', tags=('dev',))
add(G, 'Quality', 'graphics.antiBlur', 'Deinterlace / anti-blur', 'toggle',
    'Remove the blur the PS2 added by blending its two interlaced fields.',
    'On gives a sharper picture. Turn it off only if you want the original soft look.', 1, tags=('dev',))
add(G, 'Quality', 'image.sharp', 'Sharpening', 'slider',
    'Contrast-adaptive sharpening (CAS) for crisper edges without halos.',
    'Works on the final picture. Start around 20 to 40 %; very high values make textures look grainy.', 0, max=100, unit='%')
add(G, 'Quality', 'gpu', 'Graphics card', 'gpu',
    'Which GPU renders the game when your PC has more than one.',
    'Automatic lets the game choose. Choose a card by name if the game starts on the wrong one, for example '
    'a laptop running on integrated graphics.', '', applies='restart', store='pref')
add(G, 'Quality', 'graphics.renderer', 'Renderer', 'segmented',
    'The engine that draws the game.',
    'Vulkan (paraLLEl-GS) runs on your graphics card and is the one to play with. Software draws on the CPU; it is very slow and '
    'only meant for comparing against when a graphics bug is suspected.',
    0, [opt(0, 'Vulkan', 'GPU, for playing'), opt(1, 'Software', 'CPU reference, very slow')], applies='restart', tags=('advanced',))

add(G, 'Frame rate and latency', 'graphics.vsync', 'Vertical sync', 'toggle',
    'Match frames to your monitor refresh to stop screen tearing.',
    'Recommended on. Turning it off can lower input delay a little but tear lines can appear when the camera moves.', 1,
    tags=('recommended:1',))
add(G, 'Frame rate and latency', 'graphics.frameQueue', 'Frame queue', 'slider',
    'Frames the GPU may prepare ahead of what you see.',
    '1 is off: the lowest input delay, and the default. Each extra frame smooths out short GPU hiccups but adds about 16 ms of '
    'input delay. Full-screen movies queue one frame on their own.', 1, min=1, max=6, unit=' frames')
add(G, 'Frame rate and latency', 'graphics.fpsCap', 'Frame rate limit', 'slider',
    'Highest frame rate the window will present.',
    'Game logic and audio always run at the original 50/60 Hz rate, so this never changes game speed. Frame pacing for rates above the '
    'game clock is still in development; leave it at your screen refresh rate.', 60, min=20, max=240, unit=' fps', tags=('dev',))
add(G, 'Frame rate and latency', 'game.hz', 'Output refresh rate', 'segmented',
    'How many frames per second the window shows.',
    'Game speed, animation and audio always stay on the region clock (PAL 50 / NTSC 60), so 60 Hz on the PAL game never speeds anything up; it gives headroom.',
    0, [opt(0, 'Region default', 'PAL 50 / USA 60'), opt(1, '50 Hz'), opt(2, '60 Hz')], max=2, applies='restart', store='pref')
add(G, 'Frame rate and latency', 'graphics.showFps', 'FPS counter', 'toggle',
    'Show frames per second and timing in the top-left corner. F3 toggles it in game.', default=1)

for k, lab, d, det, dv, mn, mx, u in (
        ('image.brightness', 'Brightness', 'Lighten or darken the whole picture.', '', 0, -100, 100, '%'),
        ('image.contrast', 'Contrast', 'Spread or compress the gap between dark and bright.', '', 0, -100, 100, '%'),
        ('image.gamma', 'Gamma', 'Brighten or darken the midtones while keeping blacks and whites.', 'Good for TVs that look too dark in shadows.', 100, 50, 200, '%'),
        ('image.black', 'Black level', 'Push the darkest greys down to pure black.', 'Fixes a washed-out look on some displays.', 0, 0, 50, '%'),
        ('image.saturation', 'Saturation', 'Overall color strength. 0 % is black and white, 100 % is original.', '', 100, 0, 200, '%'),
        ('image.vibrance', 'Vibrance', 'Boost muted colors more than vivid ones.', 'A gentler way to add color than saturation; skin tones stay natural.', 0, -100, 100, '%'),
        ('image.temp', 'Color temperature', 'Cooler (blue) to warmer (orange).', '', 0, -100, 100, '%'),
        ('image.hue', 'Hue shift', 'Rotate every color around the color wheel.', 'Mostly for fun or display calibration; 0 is original.', 0, -180, 180, ' deg')):
    add(G, 'Image adjustments', k, lab, 'slider', d, det, dv, min=mn, max=mx, unit=u)

add(G, 'Color vision', 'access.colorblind', 'Color blind mode', 'select',
    'Correct colors that look alike so they are easier to tell apart.',
    'Pick the type that matches your vision.',
    0, [opt(0, 'Off'), opt(1, 'Protanopia', 'Red-weak'), opt(2, 'Deuteranopia', 'Green-weak'), opt(3, 'Tritanopia', 'Blue-weak')], max=3)
add(G, 'Color vision', 'access.colorblindStrength', 'Correction strength', 'slider',
    'How strongly the color correction is applied.', '', 100, max=100, unit='%')

PRESETS = [
    {'id': 'original', 'name': 'Original', 'sub': 'Exactly like a PS2', 'desc': 'Native resolution, 4:3, crisp pixels, no image changes.',
     'values': dict(NEUTRAL_IMAGE, **{'graphics.internalRes': 0, 'graphics.filter': 0, 'graphics.aspect': 0, 'graphics.integer': 0,
                                       'graphics.vsync': 1, 'graphics.frameQueue': 1})},
    {'id': 'recommended', 'name': 'Recommended', 'sub': 'Clean and smooth', 'desc': 'Native resolution with 4x anti-aliasing and light sharpening. A light cost on your GPU.',
     'values': dict(NEUTRAL_IMAGE, **{'graphics.internalRes': 1, 'graphics.filter': 0, 'graphics.aspect': 0, 'graphics.vsync': 1,
                                       'graphics.frameQueue': 1, 'image.sharp': 25})},
    {'id': 'enhanced', 'name': 'Enhanced', 'sub': 'Sharpest picture', 'desc': '2x resolution with 8x anti-aliasing, sharpening and a touch more color. Needs a stronger GPU.',
     'values': dict(NEUTRAL_IMAGE, **{'graphics.internalRes': 3, 'graphics.filter': 1, 'graphics.aspect': 0, 'graphics.vsync': 1,
                                       'graphics.frameQueue': 1, 'image.sharp': 35, 'image.vibrance': 10, 'image.contrast': 5})},
    {'id': 'performance', 'name': 'Performance', 'sub': 'Lowest GPU load', 'desc': 'Native resolution, no anti-aliasing or post effects. For laptops and handhelds.',
     'values': dict(NEUTRAL_IMAGE, **{'graphics.internalRes': 0, 'graphics.filter': 0, 'graphics.vsync': 1, 'graphics.frameQueue': 1})},
]

# ------------------------------------------------------------------ Audio
A = 'audio'
add(A, 'Output', 'audio.master', 'Master volume', 'slider', 'Overall game volume.', '', 100, max=100, unit='%')
add(A, 'Output', 'audio.mute', 'Mute', 'toggle', 'Silence the game without losing your volume settings.')
for k, lab, d in (('audio.music', 'Music', 'Menu music, entrance themes and arena music.'),
                  ('audio.commentary', 'Commentary', 'Play-by-play and color commentary.'),
                  ('audio.announcer', 'Ring announcer', 'Introductions before each match.'),
                  ('audio.crowd', 'Crowd', 'Crowd reactions, chants and arena ambience.'),
                  ('audio.stream', 'Referee and voices', 'Referee counts and other streamed speech.'),
                  ('audio.sfx', 'Effects', 'Hits, slams, weapons and menu sounds.')):
    add(A, 'Mixer', k, lab, 'fader', d, '', 100, max=100, unit='%')

add(A, 'Quality', 'audio.interp', 'Sound interpolation', 'segmented',
    'How the PS2 sound chip\'s samples are smoothed when played back.',
    'Gaussian is the original PS2 sound chip\'s filter. Linear is the alternative. Needs a game restart.',
    0, [opt(0, 'Gaussian', 'Original'), opt(1, 'Linear', 'Brighter')], applies='restart', tags=('dev',))

# ------------------------------------------------------------------ Controls
C = 'controls'
add(C, 'Controller', 'controls.deadzone', 'Stick deadzone', 'slider',
    'How far a stick must move before the game notices it.',
    'Raise it if your character walks on their own (stick drift). Lower it for more precise control. Applies to both players.',
    15, max=50, unit='%')
add(C, 'Controller', 'controls.rumble', 'Rumble', 'toggle',
    'Controller vibration on hits, slams and crowd moments.', '', 1)
add(C, 'Button prompts', 'game.iconSet', 'Button icons', 'icons',
    'Replace the PlayStation button prompts in menus with your controller\'s icons.',
    'Replaces the Cross / Square / Triangle / Circle, shoulder and D-pad prompts in menus. PAL version only for now.', 0, max=8, applies='restart',
    store='pref', tags=('pal-only',))
add(C, 'Assists', 'input.p1.mashAssist', 'Mash assist, player 1', 'toggle',
    'Hold Cross to rapid-fire it for pin and submission escapes.',
    'Instead of mashing, hold the button and the game receives presses at the speed set below.')
add(C, 'Assists', 'input.p2.mashAssist', 'Mash assist, player 2', 'toggle', 'The same assist for player 2.')
add(C, 'Assists', 'input.mashHz', 'Mash speed', 'slider', 'Presses per second while mash assist is held.',
    'Higher is easier.', 12, min=4, max=20, unit=' / s')
add(C, 'Assists', 'input.p1.sticky', 'Sticky shoulder buttons, player 1', 'toggle',
    'L1, R1, L2 and R2 stay held after one press. Press again to release.',
    'Helps when holding a shoulder button is uncomfortable.')

# ------------------------------------------------------------------ Gameplay
P = 'gameplay'
add(P, 'Start-up', 'game.skipMovies', 'Skip intro movies', 'toggle',
    'Go straight to the title screen without the logos and opening movie.', '', 0, applies='restart', store='pref')
add(P, 'Extras', 'game.arcade', 'PSP arcade extras', 'toggle',
    'Reserved for the PSP Arcade modes. WWE Texas Hold\'em is already playable without it: press F1 in game, Enhancements, Gameplay.',
    'Hold\'em uses the table, cards and chips from your PSP game, so the PSP game must be linked once on the Library page. '
    'Game Show is playable too (questions, pictures and text read from your PSP game); Eugene Airplane Race is not built yet.', 0, applies='restart', store='pref', tags=('needs-psp',))
add(P, 'Performance and diagnostics', 'game.mode', 'Emulation mode', 'segmented',
    'Speed-ups for the PS2 vector unit that draws characters and the arena.',
    'Normal uses the vector-unit speed-ups and is what to play with. Safe turns them off: slower, but it is the reference path, useful to check whether a glitch is caused by a speed-up.', 0, [opt(0, 'Normal', 'Fast, recommended'), opt(1, 'Safe', 'Reference path, slower')],
    applies='restart', store='pref', tags=('recommended:0',))
add(P, 'Performance and diagnostics', 'game.probes', 'Diagnostic logging', 'toggle',
    'Write extra lines to the log so bugs can be tracked down.',
    'Bug reports are much more useful with it on. Turning it off is a little faster.', 1,
    applies='restart', store='pref')

add(P, 'Game tweaks', 'game.noDemo', 'Disable demo timeout', 'toggle',
    'Stop the attract-mode demo from starting while you sit on the title screen.', '', 0, tags=('dev',))
add(P, 'Game tweaks', 'game.fastLoad', 'Skip loading-screen delay', 'toggle',
    'Shorten the fixed wait on loading screens.', 'The game holds loading screens for a minimum time even when loading is finished; this removes that wait.', 0, tags=('dev',))
add(P, 'Game tweaks', 'game.camera', 'Match camera', 'segmented', 'Camera distance during matches.',
    'Original is the stock camera. Wide pulls back so more of the ring is visible; Close stays near the action.',
    0, [opt(0, 'Original'), opt(1, 'Wide'), opt(2, 'Close')], max=2, tags=('dev',))
add(P, 'Game tweaks', 'game.countout', 'Count-out length', 'slider', 'How long the referee counts when someone is outside the ring.', '', 10, min=5, max=30, unit=' s', tags=('dev',))
add(P, 'Game tweaks', 'game.wideHud', 'Widescreen HUD', 'toggle', 'Keep health bars, names and meters in proportion at 16:9 and wider.', '', 0, tags=('dev',))
add(P, 'In-game menu (F1)', 'menu.highContrast', 'High contrast menu', 'toggle', 'Brighter text and a fully opaque menu background.', '', 0)
add(P, 'In-game menu (F1)', 'menu.boldText', 'Bold menu text', 'toggle', 'Heavier text in the in-game menu for easier reading.', '', 0)
add(P, 'In-game menu (F1)', 'menu.reduceMotion', 'Reduce menu motion', 'toggle', 'Turn off sliding and fading animations in the in-game menu.', '', 0)
add(P, 'In-game menu (F1)', 'menu.opacity', 'Menu background opacity', 'slider', 'How see-through the in-game menu is.', '', 88, max=100, unit='%')
add(P, 'In-game menu (F1)', 'menu.scale', 'Menu size', 'slider', 'Scale the whole in-game menu. Raise it to read from a distance.', '', 100, min=70, max=200, unit='%')

# ------------------------------------------------------------------ Mods & music
M = 'mods'
add(M, 'HD textures', 'game.texpack', 'HD texture pack', 'toggle',
    'Replace the game\'s textures with the HD pack wherever it has a match.',
    'Textures are matched like PCSX2 does, by their pixel data and palette, so a texture is only swapped when it is the exact one the pack was made for. '
    'The original shows for a moment while the HD version loads. HD textures use extra video memory. Works on the USA version and PAL; other versions are untested.',
    0, applies='restart', store='pref', tags=('usa-pal',))
add(M, 'Custom music', 'music.replace', 'Replacement tracks', 'toggle',
    'Play your own file instead of a stock song.',
    'Put the file in "Custom Music\\Replacements", named like BGM2_012.ogg. The name of the song playing right now is in Replacements\\_last_played.txt. Off keeps the original songs.')
add(M, 'Custom entrances', 'entrance.customMusic', 'Custom entrance songs', 'toggle',
    'Play the song you assigned to a wrestler when he or she enters.',
    'Wrestlers and slots in the game stay stock. Assign songs per wrestler under Mods > Custom entrances (Legends, Superstars, Divas, each A to Z). Off plays the original songs.', 1)
add(M, 'Custom entrances', 'entrance.customTitantron', 'Custom Titantrons', 'toggle',
    'Play the Titantron video you assigned to a wrestler.',
    'Put normal video files in the "Custom Titantrons" folder and pick one per wrestler under Mods > Custom entrances. Off plays the original Titantrons.', 1)
add(M, 'Custom entrances', 'entrance.customMoves', 'Custom move-sets', 'toggle',
    'Use the move-set you chose for a wrestler (for example one taken from SvR 2005).',
    'Pick a source game per wrestler under Mods > Custom entrances. Moves the old game shares with this one are copied; the rest come from a stock wrestler. Off keeps the original moves.', 1)
add(M, 'Custom music', 'music.popup', 'Now-playing popup', 'toggle',
    'Show artist and title for a few seconds when a custom song starts.', '', 1)

# ------------------------------------------------------------------ Launcher (this app)
L = 'launcher'
THEMES = [
    {'id': 'graphite', 'name': 'Graphite', 'sub': 'Dark, neutral'},
    {'id': 'midnight', 'name': 'Midnight', 'sub': 'Deep blue dark'},
    {'id': 'ringside', 'name': 'Ringside', 'sub': 'Warm dark'},
    {'id': 'canvas', 'name': 'Canvas', 'sub': 'Light'},
    {'id': 'system', 'name': 'Match Windows', 'sub': 'Light or dark'},
]
ACCENTS = [
    {'id': 'raw', 'name': 'Raw red', 'color': '#e5484d'},
    {'id': 'smackdown', 'name': 'SmackDown blue', 'color': '#3e7bfa'},
    {'id': 'gold', 'name': 'Championship gold', 'color': '#d9a42b'},
    {'id': 'ecw', 'name': 'Extreme green', 'color': '#3fb950'},
    {'id': 'violet', 'name': 'Violet', 'color': '#9b6dff'},
    {'id': 'mono', 'name': 'Monochrome', 'color': '#c8ccd4'},
]
UI_DEFAULTS = {'theme': 'graphite', 'accent': 'raw', 'textScale': 100, 'density': 'comfortable', 'motion': 'full',
               'afterLaunch': 'stay', 'startPage': 'play', 'showDetails': 'hover', 'window': None, 'tips': True}

PAGES = [
    {'id': 'play', 'name': 'Play', 'group': ''},
    {'id': 'graphics', 'name': 'Graphics', 'group': 'Settings', 'sub': 'Resolution, quality, frame rate and picture'},
    {'id': 'audio', 'name': 'Audio', 'group': 'Settings', 'sub': 'Volume and the mixer'},
    {'id': 'controls', 'name': 'Controls', 'group': 'Settings', 'sub': 'Controller, button icons and assists'},
    {'id': 'gameplay', 'name': 'Gameplay', 'group': 'Settings', 'sub': 'Start-up, extras and emulation'},
    {'id': 'cheats', 'name': 'Cheats', 'group': 'Settings', 'sub': 'Unlocks and match tweaks'},
    {'id': 'mods', 'name': 'Mods & music', 'group': 'Content', 'sub': 'HD textures and your own music'},
    {'id': 'saves', 'name': 'Saves', 'group': 'Content', 'sub': 'Memory cards and backups'},
    {'id': 'library', 'name': 'Library', 'group': 'Content', 'sub': 'Game versions and your game files'},
    {'id': 'tools', 'name': 'Tools', 'group': 'Help', 'sub': 'Logs, screenshots, health check and bug reports'},
    {'id': 'launcher', 'name': 'Launcher', 'group': 'Help', 'sub': 'Look and behavior of this app'},
]

HOTKEYS = [('F1', 'Open or close the in-game settings menu'), ('F11', 'Fullscreen on / off'), ('F3', 'FPS counter on / off')]

BY_KEY = {s['key']: s for s in S}

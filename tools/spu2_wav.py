"""Turn the raw SPU2 dump (SVR06_SPU2_WAV=<file> env var; 48 kHz stereo int16 with a 44-byte blank header)
into a playable WAV and print loudness stats so silence/clipping can be checked without listening.
usage: spu2_wav.py dump.wav [out.wav]"""
import sys, struct, array, math
src = sys.argv[1]; dst = sys.argv[2] if len(sys.argv) > 2 else src.replace('.wav', '_fixed.wav')
d = open(src, 'rb').read()[44:]
d = d[:len(d) // 4 * 4]
a = array.array('h'); a.frombytes(d)
n = len(a)
hdr = b'RIFF' + struct.pack('<I', 36 + len(d)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 2, 48000, 48000 * 4, 4, 16) + b'data' + struct.pack('<I', len(d))
open(dst, 'wb').write(hdr + d)
secs = n / 2 / 48000
peak = max((abs(x) for x in a), default=0)
rms = math.sqrt(sum(x * x for x in a) / n) if n else 0
# activity per second
win = 48000 * 2
act = [sum(1 for x in a[i:i + win:16] if abs(x) > 200) / max(1, len(a[i:i + win:16])) for i in range(0, n, win)]
print(f'{dst}: {secs:.1f} s, peak={peak}, rms={rms:.0f}, clipped={sum(1 for x in a if abs(x) >= 32767)}')
print('active fraction per second:', ' '.join(f'{x:.2f}' for x in act[:40]))
print('SILENT' if peak < 300 else 'has audio')

"""Generate two original demo loops and embed original texts for file:// use."""
import array
import json
import math
import pathlib
import lameenc

ROOT = pathlib.Path(__file__).resolve().parents[1]
ASSETS = ROOT / "simulator/assets"

def song(filename, root, melody):
    rate = 22050
    seconds = 24
    samples = [0.0] * (rate * seconds)
    for step in range(32):
        note = root + melody[step % len(melody)]
        frequency = 440 * 2 ** ((note - 69) / 12)
        start = int(step * .6875 * rate)
        for i in range(min(int(2 * rate), len(samples) - start)):
            t = i / rate
            envelope = (1 - math.exp(-t * 60)) * math.exp(-t * 2.7)
            samples[start+i] += .18 * envelope * (math.sin(2*math.pi*frequency*t) + .18*math.sin(4*math.pi*frequency*t))
    for beat in range(8):
        bass = root - 24 + [0, 5, 7, 0][beat % 4]
        start = int(beat * 2.75 * rate)
        for offset in (0, 7, 12):
            frequency = 440 * 2 ** ((bass + offset - 69) / 12)
            for i in range(min(int(3*rate), len(samples)-start)):
                t = i / rate
                samples[start+i] += .026 * min(1, t*5) * math.exp(-t*1.3) * math.sin(2*math.pi*frequency*t)
    pcm = array.array('h', (round(max(-.6, min(.6, value)) * 32767 * min(1, at/rate*3, (seconds-at/rate)/1.5)) for at,value in enumerate(samples)))
    encoder = lameenc.Encoder()
    encoder.set_bit_rate(64)
    encoder.set_in_sample_rate(rate)
    encoder.set_channels(1)
    encoder.set_quality(2)
    (ASSETS / filename).write_bytes(encoder.encode(pcm.tobytes()) + encoder.flush())

def main():
    song('afternoon.mp3', 72, [0,4,7,12,7,4,2,0,5,9,12,9,7,5,4,2])
    song('evening.mp3', 69, [0,3,7,10,7,3,5,3,0,-2,0,3,5,7,3,0])
    books = [{"name": f"00{i+1}-{p.name}", "text": p.read_text(encoding='utf-8')} for i,p in enumerate(sorted(ASSETS.glob('*.txt')))]
    (ROOT / 'simulator/demo-books.js').write_text('// Original demo texts, embedded for offline use.\nglobalThis.POCKET_DEMO_BOOKS = ' + json.dumps(books,ensure_ascii=False) + ';\n', encoding='utf-8')
    print('Generated two original MP3 loops and', len(books), 'books')

if __name__ == '__main__':
    main()

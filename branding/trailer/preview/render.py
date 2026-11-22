"""Render the local footage and HTML graphics for the five-minute review edit.

The HTML contains the edit decisions. Supply the local recordings with --media
and the path to an FFmpeg build with h264_nvenc using --ffmpeg.
"""
import argparse
import concurrent.futures
import html
import json
import re
import shutil
import subprocess
import wave
from dataclasses import dataclass
from html.parser import HTMLParser
from pathlib import Path


FPS = 60
WIDTH = 2580
HEIGHT = 1080
RATE = 48000
ROOT = Path(__file__).resolve().parent


@dataclass
class Clip:
    name: str
    source: str
    start: float
    duration: float
    offset: float
    effect: str
    timing: dict


class Composition(HTMLParser):
    def __init__(self, source):
        super().__init__()
        self.clips = []
        self.cards = []
        self.duration = 300
        self.bounds = [0,70,106,128,159,196,213,252,274,300]
        self.version = "v1"
        self.feed(source.read_text(encoding="utf-8"))
        head = 0
        for clip in self.clips:
            if round(clip.start * FPS) != head:
                raise ValueError(f"Gap before {clip.name}")
            head += round(clip.duration * FPS)
        if head != round(self.duration * FPS):
            raise ValueError(f"Expected {self.duration} seconds; got {head} frames")

    def handle_starttag(self, tag, attributes):
        data = dict(attributes)
        if tag == "main":
            self.duration = int(data["data-duration"])
            if "data-chapters" in data:
                self.bounds = [int(n) for n in data["data-chapters"].split(",")]
            self.version = data.get("data-version", "v1")
        if "data-source" in data:
            self.clips.append(Clip(
                data["id"], data["data-source"], float(data["data-start"]),
                float(data["data-duration"]), float(data["data-media-start"]),
                data.get("data-fx", "cut"), data,
            ))
        if "data-card" in data:
            self.cards.append((data["data-card"], float(data["data-start"]),
                               float(data["data-duration"])))


def run(ffmpeg, args, capture=False):
    return subprocess.run([ffmpeg, "-nostdin", "-hide_banner", *map(str, args)],
                          check=True, capture_output=capture)


def encoder(bitrate, maximum):
    return ["-c:v", "h264_nvenc", "-preset", "p5", "-tune", "hq", "-rc", "vbr",
            "-b:v", bitrate, "-maxrate", maximum, "-bufsize", maximum,
            "-spatial-aq", "1", "-g", "120", "-bf", "1", "-profile:v", "main",
            "-level:v", "5.1", "-pix_fmt", "yuv420p", "-colorspace", "bt709",
            "-color_primaries", "bt709", "-color_trc", "bt709", "-color_range", "tv"]


def verify_frames(ffmpeg, path, expected):
    result = run(ffmpeg, ["-loglevel", "error", "-i", path, "-map", "0:v:0",
                          "-c:v", "copy", "-f", "framecrc", "pipe:1"],
                 capture=True)
    count = sum(bool(re.match(rb"^\s*0,", line)) for line in result.stdout.splitlines())
    if count != expected:
        raise ValueError(f"{path.name}: expected {expected} frames, got {count}")


def render_clip(clip, args):
    output = args.work / "shots" / (clip.name + ".mkv")
    if output.exists() and not args.replace_shot:
        verify_frames(args.ffmpeg, output, round(clip.duration * FPS))
        return output
    tmp = output.with_suffix(".partial.mkv")
    length = clip.duration
    if clip.source == "black":
        inputs = ["-f", "lavfi", "-i", f"color=black:size={WIDTH}x{HEIGHT}:rate={FPS}",
                  "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo"]
        audio_input = "1:a:0"
    else:
        source = args.media / clip.source
        if not source.is_file():
            raise FileNotFoundError(source)
        inputs = ["-threads", "3", "-ss", str(clip.offset), "-i", source]
        audio_input = "0:a:0"
    video = ["fps=60"]
    if clip.effect == "zoom":
        video += ["crop=trunc(iw*.86/2)*2:trunc(ih*.86/2)*2:iw-ow:(ih-oh)/2"]
    if clip.effect == "browser":
        video += ["crop=iw:trunc(ih*.92/2)*2:0:trunc(ih*.052/2)*2",
                  "pad=iw:round(ih/.92/2)*2:0:(oh-ih)/2:color=black"]
    if clip.effect == "black-visual":
        video += ["drawbox=c=black:t=fill"]
    video += ["scale=2580:1080:flags=lanczos", "setsar=1"]
    if clip.effect == "hoof":
        video += ["drawbox=c=black:t=fill:enable='lt(t,0.7)'", "fade=in:st=0.7:d=1.8"]
    elif clip.effect == "end":
        video += ["fade=out:st=8:d=3"]
    elif args.version == "v1" and clip.start in (70, 85, 128, 196, 213, 233, 274):
        video += ["fade=in:st=0:d=0.15"]
    hold = float(clip.timing.get("data-hold","0"))
    fade_in = float(clip.timing.get("data-in","0"))
    fade_out = float(clip.timing.get("data-out","0"))
    if fade_in:
        video += [f"fade=in:st={hold}:d={fade_in}"]
    if fade_out:
        video += [f"fade=out:st={length-fade_out}:d={fade_out}"]
    video += [f"trim=end_frame={round(length * FPS)}", "settb=1/60", "setpts=N"]
    audio = ["aresample=48000:async=1:first_pts=0",
             "aformat=sample_fmts=s16:channel_layouts=stereo", "apad",
             f"atrim=end_sample={round(length * RATE)}", "asetpts=N/SR/TB"]
    if clip.source != "black":
        attack = float(clip.timing.get('data-audio-in','.045'))
        if attack:
            audio += [f"afade=in:st=0:d={attack}"]
        tail = float(clip.timing.get("data-audio-out",2 if clip.effect=="end" else .065))
        if tail:
            audio += [f"afade=out:st={length-tail:.4f}:d={tail}"]
    run(args.ffmpeg, ["-loglevel", "error", "-y", *inputs, "-t", length,
                      "-map", "0:v:0", "-map", audio_input, "-filter_threads", "3",
                      "-vf", ",".join(video), "-af", ",".join(audio),
                      *encoder("25000k", "40000k"), "-r", "60", "-fps_mode", "cfr",
                      "-enc_time_base:v", "1:60", "-c:a", "pcm_s16le",
                      "-ar", RATE, "-ac", 2, tmp])
    verify_frames(args.ffmpeg, tmp, round(length * FPS))
    tmp.replace(output)
    print(f"Ready {clip.name}, timeline {clip.start:g}s + {length:g}s", flush=True)
    return output


def concat_list(file, names):
    lines = ["ffconcat version 1.0"]
    for name, duration in names:
        escaped = name.resolve().as_posix().replace("'", "'\\''")
        lines.extend([f"file '{escaped}'", f"duration {duration:.8f}"])
    file.write_text("\n".join(lines) + "\n", encoding="utf-8")


def subtitle_sources(args):
    entries = json.loads((ROOT / "narration.json").read_text(encoding="utf-8"))
    lines, nodes = [], []
    def clock(seconds):
        ms = round(seconds * 1000)
        return f"{ms//3600000:02}:{ms//60000%60:02}:{ms//1000%60:02},{ms%1000:03}"
    last = 0
    for index, cue in enumerate(entries):
        a, b = cue["start"], cue["end"]
        if not last <= a < b <= args.duration or not cue["en"].strip() or not cue["zh"].strip():
            raise ValueError(f"Invalid bilingual cue {index + 1}")
        last = b
        lines.append(f'{index+1}\n{clock(a)} --> {clock(b)}\n{cue["zh"]}\n{cue["en"]}\n')
        nodes.append(f'  <div id="cue-{index+1:02}" class="caption" data-start="{a}" '
                     f'data-duration="{b-a:.3f}" data-track-index="6">'
                     f'<span class="zh">{html.escape(cue["zh"])}</span>'
                     f'<span class="en" lang="en">{html.escape(cue["en"])}</span></div>')
    template = ROOT / "index.html"
    source = template.read_text(encoding="utf-8")
    before, after = "  <!-- BILINGUAL CAPTIONS START -->", "  <!-- BILINGUAL CAPTIONS END -->"
    start, end = source.index(before), source.index(after)
    template.write_text(source[:start] + before + "\n" + "\n".join(nodes) + "\n" +
                        source[end:], encoding="utf-8")
    (args.work / "bilingual.srt").write_text("\n".join(lines), encoding="utf-8-sig")
    return entries


def graphical_layers(composition, entries, args):
    from PIL import Image
    layers = []
    items = [(args.work / "plates" / (name + ".png"), at, at + span, 0, 0,
              0 if name == "title" else .18, 1.2 if name == "title" else .22)
             for name, at, span in composition.cards]
    for i, cue in enumerate(entries):
        items.append((args.work / "captions" / f"cap-{i:02}.png", cue["start"],
                      cue["end"], 140, 764, 0, 0))
    folder = args.work / "trimmed-layers"
    folder.mkdir(exist_ok=True)
    for src, at, end, left, top, fade_in, fade_out in items:
        with Image.open(src) as img:
            x, y, r, b = img.getchannel("A").getbbox()
            x, y = (x//2)*2, (y//2)*2
            r, b = min(img.width, ((r+1)//2)*2), min(img.height, ((b+1)//2)*2)
            crop = folder / src.name
            img.crop((x, y, r, b)).save(crop)
        layers.append((crop, round(at*FPS), round(end*FPS), left+x, top+y,
                       round(fade_in*FPS), round(fade_out*FPS)))
    return layers


def encode_chapter(span, layers, args):
    start, end = span
    begin, finish = round(start*FPS), round(end*FPS)
    active = [layer for layer in layers if layer[1]<finish and layer[2]>begin]
    dest = args.work / "chapters" / f"picture-{start:03}.mp4"
    tmp = dest.with_suffix(".partial.mp4")
    inputs = ["-threads","4","-ss",str(start),"-i",args.work/"cut.mkv"]
    graph = ["[0:v:0]fps=60,settb=1/60,setpts=N[base0]"]
    for i, (png, a, b, x, y, entrance, exit_frames) in enumerate(active):
        inputs += ["-threads", "1", "-i", png]
        first, last = max(0, a-begin), min(finish-begin, b-begin)-1
        effects = ["format=rgba"]
        if entrance or exit_frames:
            effects += [f"loop=loop={b-a+2}:size=1:start=0","settb=1/60","setpts=N"]
            if entrance:
                effects += [f"fade=t=in:s=0:n={entrance}:alpha=1"]
            if exit_frames:
                effects += [f"fade=t=out:s={b-a-exit_frames}:n={exit_frames}:alpha=1"]
            effects += [f"setpts=PTS+({a-begin})"]
        graph.append(f"[{i+1}:v:0]" + ",".join(effects) + f"[layer{i}]")
        graph.append(f"[base{i}][layer{i}]overlay=x={x}:y={y}:format=yuv420:"
                     f"enable='between(n,{first},{last})'[base{i+1}]")
    graph.append(f"[base{len(active)}]format=yuv420p[out]")
    run(args.ffmpeg, ["-loglevel", "error", "-y", *inputs, "-filter_complex_threads", "3",
                      "-filter_complex", ";".join(graph), "-map", "[out]", "-an", "-t",
                      end-start, *encoder("9300k", "23000k"), "-r", "60", "-fps_mode",
                      "cfr", "-enc_time_base:v", "1:60", "-video_track_timescale", "60000", tmp])
    verify_frames(args.ffmpeg, tmp, finish-begin)
    tmp.replace(dest)
    print(f"Picture verified {start:g}-{end:g}s", flush=True)
    return dest, end-start


def measure(ffmpeg, path):
    proc = run(ffmpeg, ["-nostats", "-i", path, "-map", "0:a:0", "-vn", "-af",
                       "ebur128=peak=true", "-f", "null", "-"], capture=True)
    summary = proc.stderr.decode(errors="replace").split("Summary:")[-1]
    return float(re.search(r"I:\s*(-?[\d.]+) LUFS", summary).group(1))


def mix_audio(args):
    game = args.work / "cut.mkv"
    stem_name = "英文旁白.wav" if args.version=="v1" else f"英文旁白-{args.version}.wav"
    voice = args.media / "宣传片样片" / "工程素材" / stem_name
    with wave.open(str(voice)) as wav:
        if wav.getframerate()!=RATE or wav.getnframes()!=RATE*args.duration:
            raise ValueError("Narration length must match the edit at 48 kHz")
    gun = args.media / "粗剪" / "start-sound_05m52s-06m02s.wav"
    game_lufs, voice_lufs = measure(args.ffmpeg, game), measure(args.ffmpeg, voice)
    gv = min(9, max(-10, -21.5-game_lufs))
    vv = min(12, max(-15, -16.5-voice_lufs))
    print(f"Stem loudness: game {game_lufs:.1f}, voice {voice_lufs:.1f} LUFS. "
          f"Applying {gv:+.1f} and {vv:+.1f} dB.", flush=True)
    # Let the compressor flush the last hoofbeats before trimming the program.
    tail_pad = ",apad=pad_dur=3" if args.version != "v1" else ""
    graph = (f"[0:a]aresample=48000:async=1:first_pts=0,volume={gv:.2f}dB{tail_pad}[game];"
             f"[1:a]aformat=channel_layouts=stereo,volume={vv:.2f}dB{tail_pad},asplit=2[v][key];"
             "[game][key]sidechaincompress=threshold=0.025:ratio=5:attack=20:release=340[bed];"
             "[2:a]aformat=channel_layouts=stereo,volume=5dB[opening];"
             "[bed][v][opening]amix=inputs=3:duration=first:normalize=0,"
             f"alimiter=limit=0.87:level=0:latency=1,apad,atrim=end_sample={RATE*args.duration}[mix]")
    mix = args.work / "mix.wav"
    run(args.ffmpeg, ["-loglevel","error","-y","-i",game,"-i",voice,"-i",gun,
                      "-filter_complex",graph,"-map","[mix]","-ar",RATE,"-ac",2,
                      "-c:a","pcm_s16le",mix])
    return mix


def compose(composition, args):
    entries = subtitle_sources(args)
    layers = graphical_layers(composition, entries, args)
    bounds = composition.bounds
    spans = list(zip(bounds, bounds[1:]))
    (args.work / "chapters").mkdir(exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as workers:
        pictures = list(workers.map(lambda span: encode_chapter(span, layers, args), spans))
    manifest = args.work / "picture.ffconcat"
    concat_list(manifest, pictures)
    mix = mix_audio(args)
    deliver = args.media / "宣传片样片"
    output = deliver / f"The Jockey Club - 宣传片样片 {args.version}.mp4"
    if output.exists():
        raise FileExistsError(output)
    tmp = args.work / "review.checked.mp4"
    run(args.ffmpeg, ["-loglevel","error","-y","-f","concat","-safe",0,"-i",manifest,
                      "-i",mix,"-map","0:v:0","-map","1:a:0","-c:v","copy",
                      "-c:a","aac","-b:a","192k","-ar",RATE,"-ac",2,"-t",args.duration,
                      "-movflags","+faststart",tmp])
    verify_frames(args.ffmpeg, tmp, FPS*args.duration)
    run(args.ffmpeg, ["-loglevel","error","-xerror","-threads","4","-i",tmp,
                      "-map","0:v","-map","0:a","-f","null","-"])
    tmp.rename(output)
    shutil.copyfile(args.work / "bilingual.srt", output.with_suffix(".srt"))
    print(f"Verified {output.name}, {FPS*args.duration:,} frames, {args.duration} seconds.", flush=True)


def main():
    global ROOT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["shots", "assemble", "compose"])
    parser.add_argument("--media", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--skip-speed", action="store_true")
    parser.add_argument("--project", type=Path, default=ROOT)
    parser.add_argument("--replace-shot", metavar="ID",
                        help="Rebuild just this shot after moving its source in-point")
    args = parser.parse_args()
    ROOT = args.project.resolve()
    composition = Composition(ROOT / "index.html")
    args.version, args.duration = composition.version, composition.duration
    (args.work / "shots").mkdir(parents=True, exist_ok=True)
    if args.action == "shots":
        chosen = [clip for clip in composition.clips
                  if not args.skip_speed or ("速度对比.mp4" not in clip.source and "speed-v2-level" not in clip.source)]
        if args.replace_shot:
            chosen = [clip for clip in chosen if clip.name == args.replace_shot]
            if not chosen:
                parser.error(f"Unknown shot {args.replace_shot!r}")
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as workers:
            list(workers.map(lambda c: render_clip(c, args), chosen))
    elif args.action == "assemble":
        manifest = args.work / "cut.ffconcat"
        concat_list(manifest, [(args.work / "shots" / (clip.name + ".mkv"), clip.duration)
                               for clip in composition.clips])
        run(args.ffmpeg, ["-loglevel", "error", "-y", "-f", "concat", "-safe", 0,
                          "-i", manifest, "-map", "0:v", "-map", "0:a", "-c", "copy",
                          args.work / "cut.mkv"])
        verify_frames(args.ffmpeg, args.work / "cut.mkv", composition.duration * FPS)
    else:
        compose(composition, args)


if __name__ == "__main__":
    main()

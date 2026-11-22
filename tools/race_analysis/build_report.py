"""Offline race diagnostics: actual speed, corner paths and surveyed boundaries.

Inputs must be a frozen recording and a course exported by race_analysis_export.
No installed files, game state, learned values or saves are modified.
"""
import argparse
import bisect
import hashlib
import html
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from telemetry_read import records
from output_policy import publish_report


def identity(row, actor):
    return tuple(row[actor].get(key) for key in ('horse', 'model', 'rider'))


def interpolate(path, s):
    s = max(path[0][0], min(path[-1][0], s))
    i = min(len(path)-2, max(0, bisect.bisect_right([p[0] for p in path], s)-1))
    a, b = path[i:i+2]
    f = (s-a[0])/(b[0]-a[0]) if b[0] > a[0] else 0
    return [x+f*(y-x) for x, y in zip(a[1:], b[1:])]


def project(path, xy, hint=None):
    choices = []
    for a, b in zip(path, path[1:]):
        if hint is not None and (b[0] < hint-100 or a[0] > hint+100):
            continue
        dx, dy = b[1]-a[1], b[2]-a[2]
        n = dx*dx+dy*dy
        if n == 0:
            continue
        f = max(0, min(1, ((xy[0]-a[1])*dx+(xy[1]-a[2])*dy)/n))
        distance = math.hypot(xy[0]-a[1]-f*dx, xy[1]-a[2]-f*dy)
        choices.append((distance, a[0]+f*(b[0]-a[0])))
    if not choices:
        raise ValueError("No route segment around recorded progress")
    return min(choices)


def turn(path, s, window=20):
    h = window/2
    if s < path[0][0]+h or s > path[-1][0]-h:
        return None
    a, b, c = (interpolate(path, x) for x in (s-h, s, s+h))
    u, v = [b[i]-a[i] for i in (0, 1)], [c[i]-b[i] for i in (0, 1)]
    n = math.hypot(*u)*math.hypot(*v)
    return math.degrees(math.acos(max(-1, min(1, sum(x*y for x, y in zip(u, v))/n)))) if n else None


class Track:
    def __init__(self, rows, actor, max_gap=.3, max_kmh=100):
        self.s = [r['navigation'][actor+'_along_m'] for r in rows]
        if not all(math.isfinite(s) for s in self.s):
            raise ValueError(f'{actor}: non-finite qualified progress')
        if any(b < a for a, b in zip(self.s, self.s[1:])):
            raise ValueError(f'{actor}: qualified progress moved backwards; split the session before analysis')
        self.xy = [r[actor]['xyz'][:2] for r in rows]
        self.t = [(r['game_ms']-rows[0]['game_ms'])/1000 for r in rows]
        self.d, self.good = [0.], []
        for i, (a, b) in enumerate(zip(rows, rows[1:])):
            dt = self.t[i+1]-self.t[i]
            distance = math.dist(self.xy[i], self.xy[i+1])
            ok = 0 < dt <= max_gap and distance/dt*3.6 <= max_kmh
            ok = ok and identity(a, actor) == identity(b, actor) and a[actor]['horse'] != 0
            self.good.append(ok)
            self.d.append(self.d[-1]+distance)

    def at(self, s):
        if not self.s[0] <= s <= self.s[-1]:
            return None
        i = bisect.bisect_left(self.s, s)
        if i == 0:
            return [self.t[0], self.d[0], *self.xy[0]]
        if not self.good[i-1]:
            return None
        f = (s-self.s[i-1])/(self.s[i]-self.s[i-1])
        a, b = [self.t[i-1], self.d[i-1], *self.xy[i-1]], [self.t[i], self.d[i], *self.xy[i]]
        return [x+f*(y-x) for x, y in zip(a, b)]

    def complete(self, lo, hi):
        i, j = bisect.bisect_right(self.s, lo)-1, bisect.bisect_left(self.s, hi)
        return lo >= self.s[0] and hi <= self.s[-1] and all(self.good[max(0, i):j])

    def bins(self, edges):
        seconds, distance, bad = [0.]*(len(edges)-1), [0.]*(len(edges)-1), set()
        for i, (a, b) in enumerate(zip(self.s, self.s[1:])):
            j = min(len(seconds)-1, max(0, bisect.bisect_right(edges, a)-1))
            while j < len(seconds) and (edges[j] < b or a == b):
                fraction = 1 if a == b else max(0, min(b, edges[j+1])-max(a, edges[j]))/(b-a)
                if not self.good[i]:
                    bad.add(j)
                else:
                    seconds[j] += fraction*(self.t[i+1]-self.t[i])
                    distance[j] += fraction*(self.d[i+1]-self.d[i])
                j += 1
                if a == b:
                    break
        points = [[(a+b)/2, 3.6*distance[i]/seconds[i] if seconds[i] > 0 and
                   i not in bad and a >= self.s[0] and b <= self.s[-1] else None]
                  for i, (a, b) in enumerate(zip(edges, edges[1:]))]
        return points, {'samples': len(self.s), 'invalid_intervals': self.good.count(False),
                        'valid_bins': sum(y is not None for _, y in points),
                        'covered_until_m': self.s[-1], 'seconds': sum(seconds)}


def observed_goals(rows, path):
    chunks, last, previous_frame = [], None, None
    for r in rows:
        if previous_frame is not None and (not 0 < r['game_ms']-previous_frame['game_ms'] <= 300 or
                                           identity(r, 'npc') != identity(previous_frame, 'npc')):
            last = None
        previous_frame = r
        n = r['navigation']
        index, xyz = n.get('target'), n.get('goal')
        if index is None or not xyz or n.get('driver_mode') != 2 or not r['npc']['horse']:
            last = None
            continue
        if last and index == last['index'] and math.dist(xyz, last['xyz']) < .001:
            continue
        # Only join consecutively observed nodes in the same chronological run.
        # Replans, changed coordinates and skipped targets start a new chunk.
        if last is None or index != last['index']+1:
            chunks.append([])
        distance, s = project(path, xyz, n['npc_along_m']+n.get('remaining_to_target_m', 0))
        if distance > 30:
            last = None
            continue
        last = {'index': index, 's': s, 'xyz': xyz, 't': r['t']}
        chunks[-1].append(last)
    return [c for c in chunks if c]


def regions_for(course, definitions):
    result = []
    for definition in definitions:
        source = next(p for p in course['profiles'] if p['id'] == definition['profile'])
        if not 0 <= definition['from'] < definition['to'] <= source['length']:
            raise ValueError('Corner bounds outside source profile')
        endpoints = [interpolate(source['path'], definition[k]) for k in ('from', 'to')]
        for profile in course['profiles']:
            if profile['id'] != source['id'] and not definition.get('repeat_on_profiles', False):
                continue
            positions = [project(profile['path'], p) for p in endpoints]
            if max(d for d, _ in positions) > definition.get('max_projection_error_m', 10):
                raise ValueError('Corner projection is too far from another profile; supply explicit regions')
            lo, hi = [s for _, s in positions]
            if hi <= lo:
                raise ValueError('Projected corner direction differs; supply explicit regions')
            for observation in profile['observations']:
                result.append({'name': definition['name'], 'lap': observation['lap'],
                               'lo': observation['global_start']+lo,
                               'hi': observation['global_start']+hi,
                               'projection_error_m': max(d for d, _ in positions)})
    return sorted(result, key=lambda r: (r['lap'], r['lo']))


def clipped_goals(chunks, lo, hi):
    result = []
    for chunk in chunks:
        part = []
        for goal in chunk:
            if lo <= goal['s'] <= hi:
                part.append(goal)
            elif part:
                result.append(part)
                part = []
        if part:
            result.append(part)
    return result


def corner_data(regions, tracks, goals, course):
    panels, stats = [], []
    for region in regions:
        lo, hi = region['lo'], region['hi']
        if not all(t.complete(lo-2.5, hi+2.5) for t in tracks.values()):
            continue  # Never extrapolate an unfinished or interrupted corner.
        centre = [interpolate(course['path'], lo)] + [p[1:] for p in course['path'] if lo < p[0] < hi] + [interpolate(course['path'], hi)]
        plan = [[g['xyz'] for g in chunk] for chunk in clipped_goals(goals, lo, hi)]
        panel = {**region, 'centre': centre, 'plan': [c for c in plan if c],
                 'halfWidth': course['parameters']['half_width'],
                 'usableHalfWidth': course['parameters']['usable_half_width'], 'actors': {}}
        stat = {**region, 'bend': region['name'], 'actors': {}}
        for name, t in tracks.items():
            speeds = []
            for s in range(math.ceil(lo), math.floor(hi)+1):
                a, b = t.at(s-2.5), t.at(s+2.5)
                speeds.append(3.6*(b[1]-a[1])/(b[0]-a[0]))
            a, b = t.at(lo), t.at(hi)
            raw = [(lo, a[2:])] + [(s, p) for s, time, p in zip(t.s, t.t, t.xy)
                                  if a[0] < time < b[0]] + [(hi, b[2:])]
            points = []
            for s, p in raw:
                begin, end = t.at(s-2.5), t.at(s+2.5)
                points.append([s, *p, 3.6*(end[1]-begin[1])/(end[0]-begin[0])])
            xy = [p for _, p in raw]
            own = [[0, *xy[0]]]
            for p in xy[1:]:
                distance = math.dist(own[-1][1:], p)
                if distance > 1e-6:
                    own.append([own[-1][0]+distance, *p])
            angles = [v for s in range(10, math.floor(own[-1][0]-10)+1) if (v := turn(own, s)) is not None]
            stat['actors'][name] = {'seconds': b[0]-a[0], 'min5mKmh': min(speeds),
                                    'curvature': {'max20mAngle': max(angles, default=0)}}
            panel['actors'][name] = {'points': points, 'seconds': b[0]-a[0]}
        stat['extraSeconds'] = stat['actors']['rival']['seconds']-stat['actors']['you']['seconds']
        panels.append(panel)
        stats.append(stat)
    return {'panels': panels, 'stats': stats}


def rounded(v):
    if isinstance(v, float):
        return round(v, 4)
    if isinstance(v, list):
        return [rounded(x) for x in v]
    if isinstance(v, dict):
        return {k: rounded(x) for k, x in v.items()}
    return v


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--recording', required=True, type=Path)
    cli.add_argument('--course', required=True, type=Path)
    cli.add_argument('--regions', type=Path)
    cli.add_argument('--title', required=True)
    cli.add_argument('--rival-name', default='Host')
    cli.add_argument('--output', required=True, type=Path)
    cli.add_argument('--visuals', required=True, type=Path)
    cli.add_argument('--prefix', required=True)
    cli.add_argument('--bin-metres', type=float, default=5)
    args = cli.parse_args()
    if args.bin_metres <= 0:
        cli.error('--bin-metres must be positive')
    rows = [r for r in records(str(args.recording)) if r.get('racing')]
    if len(rows) < 2:
        raise ValueError('Recording has fewer than two racing samples')
    course = json.loads(args.course.read_text(encoding='utf-8-sig'))
    length = course['length']
    for r in rows:
        n = r['navigation']
        if abs(n['course_length_m']-length) > .05 or n['total_laps'] != len(course['lap_offsets'])-1:
            raise ValueError('Course expansion/lap count differs from the recorded race')
    tracks = {name: Track(rows, key) for name, key in [('you', 'player'), ('rival', 'npc')]}
    edges = [i*args.bin_metres for i in range(math.ceil(length/args.bin_metres))] + [length]
    series, checks = {}, {}
    for name, track in tracks.items():
        series[name], checks[name] = track.bins(edges)
    regions = regions_for(course, json.loads(args.regions.read_text(encoding='utf-8')) if args.regions else [])
    goals = observed_goals(rows, course['path'])
    common = {'source': args.recording.name, 'title': args.title,
              'names': {'you': '你', 'rival': args.rival_name},
              'subtitle': f"{len(course['lap_offsets'])-1} 圈 · {length:,.1f} m · {args.recording.name}"}
    speed = {**common, 'length': length, 'laps': course['lap_offsets'], 'series': series,
             'turn': [[(a+b)/2, turn(course['path'], (a+b)/2)] for a, b in zip(edges, edges[1:])],
             'timing': [[(a+b)/2, *[(t.at((a+b)/2) or [None])[0] for t in tracks.values()]] for a, b in zip(edges, edges[1:])],
             'coverage': {k: t.s[-1] for k, t in tracks.items()}, 'annotations': [],
             'binMetres': args.bin_metres, 'turnWindowMetres': 20}
    for region in regions:
        available = [p for p in series['rival'] if region['lo'] <= p[0] <= region['hi'] and p[1] is not None]
        if available:
            x, y = min(available, key=lambda p: p[1])
            speed['annotations'].append({'x': x, 'y': y, 'label': str(region['lap'])+region['name']})
    corners = {**common, **corner_data(regions, tracks, goals, course)}
    survey = {**common, 'parameters': course['parameters'], 'profiles': course['profiles'],
              'survey_available': course['survey_available']}
    for profile in survey['profiles']:
        for observation in profile['observations']:
            lo, hi = observation['global_start'], observation['global_end']
            observation['plan'] = [[[g['s']-lo, *g['xyz'][:2]] for g in chunk]
                                   for chunk in clipped_goals(goals, lo, hi)]
    generated = []
    for kind, data in [('speed', speed), ('corners', corners), ('survey', survey)]:
        data = rounded(data)
        publish_report(args.output/f'{kind}.json', json.dumps(data, ensure_ascii=False, separators=(',', ':')))
        if kind == 'corners' and not corners['panels']:
            continue
        template = (Path(__file__).parent/f'{kind}.template.html').read_text(encoding='utf-8')
        assert template.count('__DATA__') == 1
        fragment = template.replace('__DATA__', json.dumps(data, ensure_ascii=False, separators=(',', ':')).replace('<', '\\u003c'))
        fragment = fragment.replace('__TITLE__', html.escape(args.title)).replace('__RIVAL__', html.escape(args.rival_name))
        destination = args.visuals/f'{args.prefix}-{kind}.html'
        if len(fragment.encode('utf-8')) >= 1_000_000:
            raise ValueError('Inline report exceeds 1 MB; use a shorter race or separate profiles')
        publish_report(destination, fragment)
        generated.append(str(destination))
    provenance = {'schema': 'race-analysis.report.v1', 'inputs': [
        {'path': str(p.resolve()), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
        for p in [args.recording, args.course, args.regions] if p], 'validation': checks,
        'regions': regions, 'outputs': generated,
        'method': 'Racing samples only; actual XY displacement / game-clock time allocated into independent qualified-progress bins. '
                  'Gaps, identity changes and partial bins remain missing. Curvature uses 20 m XY chords. '
                  'Corner bounds are projected separately onto each recorded profile. Planned segments are only consecutively observed native targets. '
                  'Survey is the supplied cache snapshot, NOT the constraints at GO; route/cache geometry is validated by the C++ exporter. '
                  'Recording is checked against expanded length and lap count, not a recorded geometry hash.'}
    publish_report(args.output/'provenance.json', json.dumps(provenance, ensure_ascii=False, indent=2))
    print(json.dumps({'outputs': generated, 'validation': checks}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()

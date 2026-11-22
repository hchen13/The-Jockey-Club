"""Display a fresh production-solver plan on its matching survey snapshot.

The exporter must have been run with explicit planning parameters. This report
contains no inferred GO trajectory and does not modify installed game files.
"""
import argparse
import hashlib
import html
import json
from pathlib import Path

from build_report import interpolate, rounded
from output_policy import publish_report


def clip_plan(points, lo, hi):
    if not points[0][0] <= lo < hi <= points[-1][0]+.01:
        raise ValueError('Profile lies outside the expanded production plan')
    cut = [[lo, *interpolate(points, lo)]]
    cut += [p for p in points if lo < p[0] < hi]
    cut += [[hi, *interpolate(points, hi)]]
    return [[p[0]-lo, *p[1:]] for p in cut]


def clip_recorded(points, recorded, lo, hi):
    """Keep explicit final fallback chords, including partial lap-edge chords."""
    if len(recorded) != len(points)-1:
        raise ValueError('Final recorded-chord flags do not match plan segments')
    output = []
    for index, flag in enumerate(recorded):
        if not flag:
            continue
        start, end = max(lo, points[index][0]), min(hi, points[index+1][0])
        if start < end:
            output.append(clip_plan(points[index:index+2], start, end))
            for point in output[-1]:
                point[0] += start-lo
    return output


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--course', type=Path, required=True)
    cli.add_argument('--source-commit', required=True)
    cli.add_argument('--title', required=True)
    cli.add_argument('--output', type=Path, required=True)
    cli.add_argument('--visual', type=Path, required=True)
    args = cli.parse_args()
    course = json.loads(args.course.read_text(encoding='utf-8-sig'))
    planning = course.get('planning', {})
    if planning.get('kind') != 'offline-replan' or not planning.get('points'):
        raise ValueError('Exporter output does not contain an offline production plan')
    points = planning['points']
    recorded = planning.get('recorded_chords')
    if recorded is None:
        raise ValueError('Exporter output lacks final recorded-chord flags')
    for profile in course['profiles']:
        for obs in profile['observations']:
            obs['computedPlan'] = clip_plan(points, obs['global_start'], obs['global_end'])
            obs['recordedPlan'] = clip_recorded(points, recorded, obs['global_start'], obs['global_end'])
            obs['plan'] = []
    course_hash = hashlib.sha256(args.course.read_bytes()).hexdigest()
    data = {**course, 'source': f'offline-{args.source_commit}-{course_hash[:12]}',
            'title': args.title, 'planning': {k: v for k, v in planning.items() if k != 'points'},
            'source_commit': args.source_commit, 'observed_targets': False}
    template = (Path(__file__).parent/'survey.template.html').read_text(encoding='utf-8')
    fragment = template.replace('__DATA__', json.dumps(rounded(data), ensure_ascii=False,
                                                     separators=(',', ':')).replace('<', '\\u003c'))
    fragment = fragment.replace('__TITLE__', html.escape(args.title)).replace('__RIVAL__', '亚瑟')
    if len(fragment.encode('utf-8')) >= 1_000_000:
        raise ValueError('Inline report exceeds 1 MB')
    publish_report(args.visual, fragment)
    publish_report(args.output/'plan-overlay.json', json.dumps(rounded(data), ensure_ascii=False,
                                                             separators=(',', ':')))
    result = {'course': str(args.course.resolve()), 'course_sha256': course_hash,
              'source_commit': args.source_commit,
              'planning': {k: v for k, v in data['planning'].items() if k != 'recorded_chords'},
              'final_recorded_chords': sum(bool(flag) for flag in recorded),
              'visual': str(args.visual.resolve()), 'method':
              'Production solver on the complete expanded course, then clipped at source-progress '
              'lap boundaries with linear endpoint interpolation. Original output chords are drawn '
              'without graphical smoothing. This is a replan from the supplied snapshot, not a GO recording.'}
    publish_report(args.output/'plan-provenance.json', json.dumps(result, ensure_ascii=False, indent=2))
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()

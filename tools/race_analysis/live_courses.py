"""Read-only installed-course adapter for the production offline exporter.

No plans are computed by catalog/prepare. Cache geometry is checked by the
native profile-bank parser against each recorded shape, never by lap-count filename
alone. Historical shape and expanded maps are never imported into current evidence.
Current maps combine the shared production native base and physical evidence loader.
No road6 cache, spatial resampling, or alternative planner is used.
"""
import argparse
import configparser
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
from event_names import host_names, race_names
from analysis_process import run_analysis
from output_policy import bounded_workspace, bounded_write, OutputCapacityError
from storage_retention import StorageRetention
from clubhouse_runtime import InstalledLayout, helper_path

DEFAULT_TRACKING_MARGIN = 1.5

HOSTS = [
    ('emerald-valentine', 'HorseRaceRoutes.ini', 11), ('adler-strawberry', 'SadieRaceRoutes.ini', 12),
    ('valentine-circuit', 'CharlesRaceRoutes.ini', 13), ('rhodes-vanhorn', 'PearsonRaceRoutes.ini', 14),
    ('veteran-trail', 'VeteranCabinRaceRoutes.ini', 15), ('blackwater', 'BlackwaterRaceRoutes.ini', 16),
    ('new-austin', 'NewAustinRaceRoutes.ini', 17), ('arthur-camp', 'ArthurCampRaceRoutes.ini', 19),
    ('arthur-camp-duel', 'ArthurCampDuelRoutes.ini', 20), ('john-stakes', 'JohnStakesRaceRoutes.ini', 21),
    ('arabian-stakes', 'ArabianRaceRoutes.ini', 22), ('appaloosa-stakes', 'AppaloosaRaceRoutes.ini', 23),
    ('standardbred-stakes', 'StandardbredRaceRoutes.ini', 24), ('paint-stakes', 'PaintRaceRoutes.ini', 25),
    ('tennessee-stakes', 'TennesseeRaceRoutes.ini', 26), ('new-austin-javier-duel', 'JavierDuelRaceRoutes.ini', 27),
    ('scarlett-meadows-bill-duel', 'BillDuelRaceRoutes.ini', 28),
]
BOOKMAKERS = [
    ('valentine-circuit', 'CharlesRaceRoutes.ini', '瓦伦丁让磅赛', 'Valentine Handicap', 2),
    ('strawberry-wfa', 'StrawberryWfaRoutes.ini', '草莓镇平磅赛', 'Strawberry WFA', None),
    ('rhodes-handicap', 'RhodesHandicapRoutes.ini', '罗兹镇让磅赛', 'Rhodes Handicap', None),
    ('macfarlane-wfa', 'MacfarlaneWfaRoutes.ini', '麦克法伦牧场平磅赛', "Macfarlane's Ranch WFA", None),
]
_CATALOG_CACHE = {}


def stable_read(path):
    path = Path(path)
    for _ in range(3):
        before = path.stat()
        content = path.read_bytes()
        after = path.stat()
        if (before.st_mtime_ns, before.st_size) == (after.st_mtime_ns, after.st_size) and len(content) == after.st_size:
            return content, {'path': str(path.resolve()), 'sha256': hashlib.sha256(content).hexdigest(),
                             'mtime_ns': after.st_mtime_ns, 'size': len(content)}
    raise ValueError(f'Input changed while being read: {path}')


def parse_ini(text):
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    parser.optionxform = str
    parser.read_string(text)
    return parser


def value(config, section, key, default=''):
    for name in config.sections():
        if name.lower() == section.lower():
            for option, result in config.items(name):
                if option.lower() == key.lower():
                    return result.strip()
    return default


def tracking_margin(settings):
    """Match config::ReadInt(150, 0, 500), then convert centimetres to metres."""
    raw = value(settings, 'RaceAI', 'TrackingMarginCentimetres')
    if len(raw) >= 31 or not re.fullmatch(r'-?[0-9]+', raw):
        return DEFAULT_TRACKING_MARGIN
    centimetres = int(raw)
    return centimetres / 100 if 0 <= centimetres <= 500 else DEFAULT_TRACKING_MARGIN


def set_value(config, section, key, text):
    target = next((s for s in config.sections() if s.lower() == section.lower()), section)
    if not config.has_section(target):
        config.add_section(target)
    option = next((k for k in config[target] if k.lower() == key.lower()), key)
    config[target][option] = str(text)


def route_config(path, inputs, seen=None):
    path = Path(path).resolve()
    seen = set() if seen is None else seen
    if path in seen:
        raise ValueError('Cyclic route overlay')
    seen.add(path)
    content, info = stable_read(path)
    inputs[str(path)] = info
    overlay = parse_ini(content.decode('utf-8-sig'))
    base_name = value(overlay, 'Overlay', 'Base')
    if not base_name:
        return overlay
    base_path = (path.parent / base_name).resolve()
    if base_path.parent != path.parent:
        raise ValueError('Overlay base must be in the same configuration directory')
    result = route_config(base_path, inputs, seen)
    for section in overlay.sections():
        if section.lower() != 'overlay':
            for key, text in overlay.items(section):
                set_value(result, section, key, text)
    return result


def ini_text(config):
    stream = io.StringIO()
    config.write(stream, space_around_delimiters=False)
    return stream.getvalue()


def exporter_path():
    path = helper_path('JOCKEY_RACE_EXPORTER', 'race_analysis_export.exe')
    if not path.is_file():
        raise FileNotFoundError(f'Build the race_analysis_export target first: {path}')
    return path.resolve()


def manifest():
    courses = [{'id': f'host/{key}', 'station': key, 'route_file': route, 'kind': 'host',
                'site_ids': [site], 'fixed_extra': None} for key, route, site in HOSTS]
    courses += [{'id': f'bookmaker/{key}', 'station': key, 'route_file': route, 'kind': 'bookmaker',
                 'site_ids': [], 'title': title, 'title_en': english, 'fixed_extra': fixed}
                for key, route, title, english, fixed in BOOKMAKERS]
    return courses


def display_names(entry, config):
    """Name hosted events by eligibility/host; keep bookmaker venue names."""
    if entry['kind'] != 'host':
        return dict(title=entry['title'], title_en=entry['title_en'], event_kind='bookmaker')
    host, breed = value(config, 'Route', 'Host'), value(config, 'Route', 'Breed')
    en, zh = host_names(host)
    return {**race_names(host, breed), 'event_kind': 'challenge' if breed else 'duel',
            'breed': breed, 'host_name_en': en, 'host_name_zh': zh}


def workspace_outputs(corridor_bytes=None, shape_bytes=None):
    names = {'route.ini', 'course.json', 'survey.txt', 'pins.txt', 'courses/', 'shapes/'}
    names.update('courses/' + name for name in (corridor_bytes or {}))
    names.update('shapes/' + name for name in (shape_bytes or {}))
    return names


def _native(prepared, folder, *, plan=False, map_bytes=None, shape_bytes=None, corridor_bytes=None,
            executable=None, solver_args=()):
    folder = Path(folder)
    payloads = [prepared['route_text'].encode('utf-8')]
    if map_bytes is not None:payloads.append(map_bytes)
    if shape_bytes is not None:payloads.extend(shape_bytes.values())
    if corridor_bytes is not None:payloads.extend(corridor_bytes.values())
    if plan and prepared.get('pins_text'):payloads.append(prepared['pins_text'].encode('utf-8'))
    if len(payloads)>1024 or any(len(value)>64*1024*1024 for value in payloads) or sum(map(len,payloads))>256*1024*1024:
        raise OutputCapacityError('Course input snapshot exceeds its bounded workspace; original measurements preserved')
    route = folder / 'route.ini'
    route.write_text(prepared['route_text'], encoding='utf-8')
    survey = '-'
    if map_bytes is not None:
        survey = str(folder / 'survey.txt')
        Path(survey).write_bytes(map_bytes)
    output = folder / 'course.json'
    command = [str(executable or exporter_path()), '--live', str(route), survey, prepared['canonical_station'],
               str(prepared['runtime_parts']), str(prepared['half_width']), str(prepared['margin']),
               str(prepared['body_clearance']), str(output)]
    if plan:
        command += [str(prepared['aggression']), str(prepared['raw_speed']), str(prepared['raw_acceleration']), str(prepared['lateral'])]
    if shape_bytes is not None:
        shapes = folder / 'shapes'
        shapes.mkdir()
        for name, content in shape_bytes.items():
            (shapes / name).write_bytes(content)
        command += ['--profiles', str(shapes)]
    if corridor_bytes is not None:
        course_dir = folder / 'courses'
        for relative, content in corridor_bytes.items():
            destination = course_dir / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(content)
        course_dir.mkdir(exist_ok=True)
        command += ['--native-profiles', str(course_dir)]
    if plan and prepared.get('pins_text'):
        pins = folder / 'pins.txt'
        pins.write_text(prepared['pins_text'], encoding='utf-8')
        command += ['--pins', str(pins)]
    if plan and prepared['terrain_caution'] >= 0:
        command += ['--terrain-caution', str(prepared['terrain_caution'])]
    command += list(solver_args)
    process = run_analysis(command, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
    if process.returncode:
        raise ValueError(process.stderr.strip() or process.stdout.strip() or 'Production exporter failed')
    return json.loads(output.read_text(encoding='utf-8-sig'))


def prepare(game_root, course_id, *, total_laps=None):
    root = Path(game_root).resolve()
    layout = InstalledLayout(root)
    entry = next((item for item in manifest() if item['id'] == course_id), None)
    if entry is None:
        raise KeyError(course_id)
    inputs = {}
    config = route_config(layout.config / entry['route_file'], inputs)
    entry = dict(entry)
    entry.update(display_names(entry, config))
    mode = value(config, 'Route', 'Mode', 'Destination')
    recorded = mode.lower() in ('circuit', 'recordedlaps', 'singlelap', 'singletrip') or any(s.lower() == 'course' for s in config.sections())
    host_name = value(config, 'Route', 'Host') if entry['kind'] == 'host' else {
        'valentine-circuit': 'Clay Davies', 'strawberry-wfa': 'Bookmaker', 'rhodes-handicap': 'Clay Davies', 'macfarlane-wfa': 'Sadie Adler'}[entry['station']]
    entry.update(game_root=str(root), mode=mode, host_name=host_name, single_loop=mode.lower() == 'singlelap',
                 plannable=recorded, reason='' if recorded else '此赛事允许自由选路，没有固定录制走廊，无法计算固定走线。实跑轨迹仍可查看。')
    if not recorded:
        entry.update(input_files=inputs, survey_step_metres=3, limitations=[])
        return entry
    canonical = 'arthur-camp' if entry['station'] == 'john-stakes' else entry['station']
    settings_path = layout.main_config
    settings = parse_ini('')
    if settings_path.exists():
        content, info = stable_read(settings_path)
        inputs[str(settings_path.resolve())] = info
        settings = parse_ini(content.decode('utf-8-sig'))
    half = float(value(config, 'Circuit', 'TrackHalfWidth', '14'))
    if entry['kind'] == 'host':
        half = float(value(settings, 'CorridorHalfWidth', canonical, str(half)))
    if not 5 <= half <= 20:
        raise ValueError('Configured corridor width outside production range')
    single = mode.lower() in ('singlelap', 'singletrip') or mode.lower() == 'destination'
    minimum = int(value(config, 'Circuit', 'ExtraLapsMin', '0')) + 2
    maximum = int(value(config, 'Circuit', 'ExtraLapsMax', str(minimum-2))) + 2
    if entry['fixed_extra'] is not None:
        minimum = maximum = entry['fixed_extra'] + 2
    if total_laps is not None and (not isinstance(total_laps, int) or not 1 <= total_laps <= 64):
        raise ValueError('Invalid requested actual lap count')
    if entry['fixed_extra'] is not None and total_laps is not None and total_laps != minimum:
        raise ValueError('Requested lap count conflicts with the production fixed venue policy')
    if single and not entry['single_loop'] and total_laps not in (None, 1):
        raise ValueError('A single trip has exactly one recorded run')
    if single:
        minimum = maximum = 1
    totals = [1] if single else ([total_laps] if total_laps is not None else list(range(minimum, maximum+1)))
    # A planning overview includes each recorded profile, even if today's INI
    # requests only first+final. This changes only the temporary analysis input.
    # Historical replay retains its explicit actual count; fixed venues keep
    # their policy. No installed route or gameplay lap setting is rewritten.
    if total_laps is None and not single and entry['fixed_extra'] is None:
        totals = [3] + [n for n in totals if n != 3]
    entry.update(canonical_station=canonical, half_width=half, margin=tracking_margin(settings), body_clearance=.5,
                 aggression=max(0, min(100, float(value(settings, 'RaceAI', 'RacingLinePercent', '100'))))/100,
                 raw_speed=9, raw_acceleration=9, lateral=30, terrain_caution=0 if canonical == 'paint-stakes' else -1,
                 requested_total_laps=total_laps, configured_total_laps=[minimum, maximum],
                 survey_step_metres=3, input_files=inputs, limitations=[
                     'Button plans use explicit displayed attributes/aggression, not a reconstruction of a prior GO random draw.',
                     'Survey persistence uses native 3 m sections; coordinates and samples are not binned or densified as evidence.'])
    if canonical == 'paint-stakes':
        entry['limitations'].append('Paint runtime randomly draws steady/cautious terrain style per rider; default button variant is steady (0). Set terrain_caution=1 for the actual production cautious stage.')
    pins = layout.courses / f'{canonical}.pins.txt'
    if entry['kind'] == 'host' and pins.exists():
        content, info = stable_read(pins)
        inputs[str(pins.resolve())] = info
        entry['pins_text'] = content.decode('utf-8-sig')
    folder = layout.courses
    # Freeze native/evidence candidates; the shared production loader selects
    # exact coordinate keys. Root survey caches remain historical only.
    profile_pattern = re.compile(re.escape(canonical) + r'-shape[012]-\d+-\d+-\d+\.txt$')
    historical_pattern = re.compile(re.escape(canonical) + r'-\d+-\d+\.txt$')
    corridor_bytes = {}
    corridor_inputs = {}
    for kind in ('native', 'evidence'):
        for file in sorted((folder / kind).glob(f'{canonical}-*.txt')):
            if not (profile_pattern.fullmatch(file.name) or historical_pattern.fullmatch(file.name)):
                continue
            content, info = stable_read(file)
            corridor_bytes[f'{kind}/{file.name}'] = content
            corridor_inputs[str(file.resolve())] = info
    # Seam identity is an ordered pair of canonical profile keys. Keep the
    # nested path in the immutable snapshot reconstructed for every exporter.
    seam_pattern = re.compile(re.escape(canonical) + r'-shape[01]-\d+-\d+-\d+--' +
                              re.escape(canonical) + r'-shape[12]-\d+-\d+-\d+\.txt$')
    if not single:
        for file in sorted((folder / 'native' / 'seams').glob(f'{canonical}-*.txt')):
            if not seam_pattern.fullmatch(file.name):
                continue
            content, info = stable_read(file)
            corridor_bytes[f'native/seams/{file.name}'] = content
            corridor_inputs[str(file.resolve())] = info
    historical = sorted(str(file.resolve()) for file in folder.glob(f'{canonical}-*.txt')
                        if historical_pattern.fullmatch(file.name))
    rejected = []
    for total in totals:
        effective = parse_ini(ini_text(config))
        if not single:
            set_value(effective, 'Circuit', 'ExtraLapsMin', total-2)
            set_value(effective, 'Circuit', 'ExtraLapsMax', total-2)
        entry.update(runtime_parts=total, actual_laps=total_laps or total,
                     total_laps=total_laps or total, route_text=ini_text(effective))
        try:
            with bounded_workspace(root, 'course-probe', expected=workspace_outputs(corridor_bytes)) as temp:
                boundary = _native(entry, temp, corridor_bytes=corridor_bytes)
        except ValueError as error:
            rejected.append(str(error))
            continue
        def selected_paths(key, kind):
            return [str((folder / kind / Path(name).name).resolve()) for name in boundary.get(key, [])]
        selected = selected_paths('survey_files', 'evidence')
        native_selected = selected_paths('native_files', 'native')
        seam_selected = selected_paths('native_seam_files', 'native/seams')
        inputs.update({path: corridor_inputs[path] for path in selected + native_selected + seam_selected})
        boundary['survey_files'] = selected
        boundary['native_files'] = native_selected
        boundary['native_seam_files'] = seam_selected
        entry.update(survey_files=selected, native_files=native_selected, native_seam_files=seam_selected,
                     historical_survey_files=historical,
                     survey_source=boundary.get('survey_source', 'profile-bank'),
                     cache={'kind':'native-profile-bank', 'files':native_selected + seam_selected + selected}, boundary=boundary,
                     input_files=inputs, rejected_cache_count=len(rejected),
                     exporter=str(exporter_path()),
                     source_precision='native-float-coordinates; persistent profile survey step 3 m')
        return entry
    raise ValueError('No valid production course expansion: ' + '; '.join(rejected[-3:]))


def freeze_inputs(prepared):
    """Take one verified byte snapshot for a whole comparison, including evidence."""
    if not prepared['plannable']:
        raise ValueError(prepared['reason'])
    snapshots = {}
    for path, expected in prepared['input_files'].items():
        content, actual = stable_read(path)
        if actual['sha256'] != expected['sha256']:
            raise ValueError('Installed input changed; refresh prepare before export: ' + path)
        snapshots[path] = content
    return {f'{kind}/{Path(path).name}': snapshots[path]
                for key, kind in (('native_files', 'native'), ('native_seam_files', 'native/seams'), ('survey_files', 'evidence'))
                for path in prepared.get(key, [])}


def export_course(prepared, output, *, plan=False):
    corridor = freeze_inputs(prepared)
    if plan:
        with bounded_workspace(prepared.get('game_root', Path(output).parent), 'course-export',
                               expected=workspace_outputs(corridor)) as temp:
            result = _native(prepared, temp, plan=True, corridor_bytes=corridor)
    else:
        result = json.loads(json.dumps(prepared['boundary']))
    result['survey_files'] = prepared['survey_files']
    result['native_files'] = prepared.get('native_files', [])
    result['native_seam_files'] = prepared.get('native_seam_files', [])
    result['live'] = {key: prepared[key] for key in ('id', 'kind', 'mode', 'actual_laps', 'runtime_parts', 'limitations', 'input_files')}
    destination = Path(output)
    payload = json.dumps(result, ensure_ascii=False, allow_nan=False, separators=(',', ':')).encode('utf-8')
    if destination.parent.name in ('host', 'bookmaker') and destination.parent.parent.name == 'exports':
        StorageRetention.publish_export(destination.parent.parent, destination, payload)
    else:
        # Explicit developer exports use a stable requested filename. Bound its
        # folder too, without sweeping or truncating other developer artifacts.
        bounded_write(destination.parent, destination, payload, max_files=256,
                      max_bytes=256 * 1024 * 1024, max_file_bytes=32 * 1024 * 1024)
    return result


def catalog(game_root):
    # Polled by the UI: never probe maps, expand laps, spawn the exporter or
    # compute a plan here. Those operations belong to an explicit worker job.
    courses = []
    root = Path(game_root).resolve()
    config_folder = InstalledLayout(root).config
    signature = (str(config_folder), tuple((str(path), path.stat().st_mtime_ns, path.stat().st_size)
                                         for path in sorted(config_folder.glob('*.ini'))))
    cached = _CATALOG_CACHE.get(str(root))
    if cached and cached[0] == signature:
        return copy.deepcopy(cached[1])
    for item in manifest():
        try:
            config = route_config(config_folder / item['route_file'], {})
            mode = value(config, 'Route', 'Mode', 'Destination')
            recorded = mode.lower() in ('circuit', 'recordedlaps', 'singlelap', 'singletrip') or any(s.lower() == 'course' for s in config.sections())
            courses.append({**item, **display_names(item, config),
                            'host_name': value(config, 'Route', 'Host') if item['kind'] == 'host' else {
                                'valentine-circuit': 'Clay Davies', 'strawberry-wfa': 'Bookmaker', 'rhodes-handicap': 'Clay Davies', 'macfarlane-wfa': 'Sadie Adler'}[item['station']],
                            'mode': mode, 'single_loop': mode.lower() == 'singlelap',
                            'plannable': recorded, 'reason': '' if recorded else '自由选路赛事，没有固定录制走廊。'})
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            courses.append({**item, 'title': item.get('title', item['station']), 'title_en': item.get('title_en', item['station']),
                            'host_name': '', 'single_loop': False, 'plannable': False, 'reason': str(error)})
    result = {'courses': courses, 'game_root': str(root), 'survey_step_metres': 3}
    _CATALOG_CACHE[str(root)] = (signature, result)
    return copy.deepcopy(result)


if __name__ == '__main__':
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--game-root', type=Path, required=True)
    cli.add_argument('--course')
    cli.add_argument('--total-laps', type=int)
    cli.add_argument('--plan', action='store_true')
    cli.add_argument('--output', type=Path)
    args = cli.parse_args()
    if args.course:
        prepared = prepare(args.game_root, args.course, total_laps=args.total_laps)
        answer = export_course(prepared, args.output, plan=args.plan) if args.output else prepared
    else:
        answer = catalog(args.game_root)
    print(json.dumps(answer, ensure_ascii=False, separators=(',', ':')))

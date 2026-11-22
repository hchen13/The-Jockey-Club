"""Global INI settings only; native validators, versioned edits and atomic publication."""
from contextlib import contextmanager
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import re
import threading

from analysis_process import run_analysis
from clubhouse_runtime import InstalledLayout, helper_path
import live_courses
from event_names import HOSTS, host_names
from output_policy import bounded_workspace, settings_recovery_slot, SETTINGS_FILE_BYTES


class SettingsConflict(ValueError):
    pass


CONFLICT = '设置已在游戏中或其他页面改变，请重新读取。'


def digest(data):
    return hashlib.sha256(data).hexdigest()


@contextmanager
def settings_writer(path):
    """Serialize this tool's writers across local service instances."""
    if os.name != 'nt':
        yield
        return
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
    kernel.CreateMutexW.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.ReleaseMutex.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    name = 'Local\\TJC-settings-' + digest(str(path.resolve()).casefold().encode())[:32]
    handle = kernel.CreateMutexW(None, False, name)
    if not handle:
        raise SettingsConflict(CONFLICT)
    acquired = False
    try:
        acquired = kernel.WaitForSingleObject(handle, 5000) in (0, 0x80)
        if not acquired:
            raise SettingsConflict(CONFLICT)
        yield
    finally:
        if acquired:
            kernel.ReleaseMutex(handle)
        kernel.CloseHandle(handle)


@contextmanager
def protect_current_file(path):
    """Prevent in-place F11/profile writes during the short compare/replace phase.

    Staging and native validation happen before acquiring this handle. Readers
    are allowed; no game process, ledger or save file is opened. DELETE sharing
    permits atomic replacement while the old file remains pinned by this handle.
    """
    if os.name != 'nt':
        yield
        return
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    create = kernel.CreateFileW
    create.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                       wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    create.restype = wintypes.HANDLE
    close = kernel.CloseHandle
    close.argtypes = [wintypes.HANDLE]
    close.restype = wintypes.BOOL
    handle = create(str(path), 0x80000000, 0x1 | 0x4, None, 3, 0x80, None)
    if handle == wintypes.HANDLE(-1).value:
        raise SettingsConflict(CONFLICT)
    try:
        yield
    finally:
        close(handle)


def publish_settings(staged, destination, backup):
    """Replace atomically and retain the actual displaced file, not a stale copy."""
    if os.name != 'nt':
        backup.write_bytes(destination.read_bytes())
        os.replace(staged, destination)
        return
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    replace = kernel.ReplaceFileW
    replace.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.LPCWSTR,
                        wintypes.DWORD, ctypes.c_void_p, ctypes.c_void_p]
    replace.restype = wintypes.BOOL
    if not replace(str(destination), str(staged), str(backup), 0, None, None):
        raise ctypes.WinError(ctypes.get_last_error())


class GlobalSettings:
    def __init__(self, game, state, *, executable=None):
        self.game = Path(game).resolve()
        self.state = Path(state).resolve()
        self.layout = InstalledLayout(self.game)
        self.path = self.layout.main_config
        self.executable = Path(executable or helper_path('JOCKEY_SETTINGS_EXPORTER', 'race_settings_catalogue.exe'))
        self.lock = threading.RLock()

    def _context(self, course_id):
        entry = next((x for x in live_courses.manifest() if x['id'] == course_id), None)
        if entry is None:
            raise ValueError('请选择有效赛事。')
        sources = {}
        route = live_courses.route_config(self.layout.config / entry['route_file'], sources)
        mode = live_courses.value(route, 'Route', 'Mode', 'Destination').lower()
        recorded = mode in ('circuit', 'recordedlaps', 'singlelap', 'singletrip') or any(
            section.lower() == 'course' for section in route.sections())
        # Current bookmaker runtimes use route widths, not the F11 host override.
        # Do not offer a setting that would only alter the preview.
        station = ('arthur-camp' if entry['station'] == 'john-stakes' else entry['station']) \
            if recorded and entry['kind'] == 'host' else '-'
        half = float(live_courses.value(route, 'Circuit', 'TrackHalfWidth', '14'))
        if not math.isfinite(half) or not 5 <= half <= 20:
            raise ValueError('赛道宽度不在支持范围内。')
        raw_host = live_courses.value(route, 'Route', 'Host') if entry['kind'] == 'host' else ''
        host = next((key for key, names in HOSTS.items()
                     if raw_host.casefold() in (key.casefold(), names[0].casefold(), names[1])), '')
        # Runtime DifficultyTarget uses RaceRules::Field(), not Breed alone:
        # a breed-gated one-on-one still draws its ordinary host range.
        field_host = all(live_courses.value(route, 'Route', key) for key in ('Breed', 'FieldMin', 'FieldMax'))
        return {'course_id': course_id, 'station': station, 'half': half,
                'host': host, 'recorded': recorded, 'kind': entry['kind'],
                'host_selection': 'upper_bound' if field_host else 'independent',
                'sources': {name: info['sha256'] for name, info in sources.items()}}

    @staticmethod
    def _available(key, context):
        if key == 'corridor':
            return context['station'] != '-'
        if key == 'margin':
            return context['recorded']
        if key == 'line_percent':
            return context['recorded'] and context['kind'] == 'host'
        return bool(context['host']) and key.startswith('npc.' + context['host'] + '.')

    def _scope(self, answer, context):
        groups = []
        for original in answer['groups']:
            fields = [field for field in original.get('fields', []) if self._available(field['key'], context)]
            if not fields:
                continue
            group = {**original, 'fields': fields}
            if group['id'] == 'course':
                for field in fields:
                    field['help'] = ''
                if context['station'] == 'arthur-camp':
                    group['description'] = '亚瑟与约翰的纯血马赛共用此宽度。'
            if group['id'] == 'general':
                group['label'] = '走线'
                group['description'] = '此处走线参数目前由所有赛道共用。'
                for field in fields:
                    if field['key'] == 'line_percent':
                        field['label'] = '走线利用率'
            if group['id'].startswith('npc.'):
                en, zh = host_names(context['host'])
                group['label'] = {'zh': '主办人 · ' + zh, 'en': 'Host · ' + en}
                group['description'] = '主办人能力适用于其主持的所有赛事。'
                if group.get('default_ability'):
                    group['default_ability'] = {**group['default_ability'], 'selection': context['host_selection']}
            groups.append(group)
        return {**answer, 'groups': groups, 'timing': '设置于下一场比赛生效。'}

    @staticmethod
    def _revision(raw, context):
        return digest(raw + b'\0' + json.dumps(context, sort_keys=True, ensure_ascii=True).encode())

    def _tool(self, operation, ini, context, patch=None):
        if not self.executable.is_file():
            raise RuntimeError('全局配置工具尚未安装。')
        args = [str(self.executable), operation, str(ini), context['station'], str(context['half'])]
        if patch is not None:
            args.append(str(patch))
        result = run_analysis(args, capture_output=True, text=True, encoding='utf-8', timeout=15)
        if result.returncode:
            try:
                message = json.loads(result.stdout).get('error')
            except (ValueError, AttributeError):
                message = None
            raise ValueError(message or result.stderr.strip() or '配置校验失败。')
        answer = json.loads(result.stdout)
        if not isinstance(answer.get('groups'), list):
            raise ValueError('配置工具返回无效数据。')
        for group in answer['groups']:
            for field in group.get('fields', []):
                key = field['key']
                if key in ('margin', 'corridor'):
                    field['unit'] = 'm'
                elif key in ('line_percent', 'own_horse_percent') or key.startswith('bgm.'):
                    field['unit'] = '%'
                elif key == 'shared_cornering' or key.endswith('.cornering'):
                    field['unit'] = 'm/s²'
        return self._scope(answer, context)

    def snapshot(self, course_id):
        with self.lock:
            context = self._context(course_id)
            raw, _ = live_courses.stable_read(self.path)
            if len(raw) > SETTINGS_FILE_BYTES:
                raise ValueError('设置文件超过读取容量；原配置已保留。')
            with bounded_workspace(self.path, 'settings-read', expected=['settings.ini']) as directory:
                staged = Path(directory) / 'settings.ini'
                staged.write_bytes(raw)
                result = self._tool('describe', staged, context)
            return {**result, 'course_id': course_id, 'revision': self._revision(raw, context)}

    def apply(self, course_id, revision, changes):
        if not isinstance(revision, str) or not re.fullmatch(r'[a-f0-9]{64}', revision):
            raise ValueError('请先读取设置。')
        if not isinstance(changes, dict) or not 0 < len(changes) <= 160:
            raise ValueError('没有可保存的设置。')
        patch_lines = []
        for key, value in changes.items():
            if not isinstance(key, str) or not re.fullmatch(r'[A-Za-z0-9_.]+', key):
                raise ValueError('不支持的设置。')
            if value is not None and not isinstance(value, (bool, int, float)):
                raise ValueError('设置值无效。')
            if isinstance(value, int) and not -10000 <= value <= 10000:
                raise ValueError('设置值无效。')
            if isinstance(value, float) and not math.isfinite(value):
                raise ValueError('设置值无效。')
            patch_lines.append(key + '\t' + json.dumps(value, allow_nan=False))
        with self.lock, settings_writer(self.path):
            context = self._context(course_id)
            if any(not self._available(key, context) for key in changes):
                raise ValueError('此项不属于当前赛事。')
            raw, _ = live_courses.stable_read(self.path)
            if self._revision(raw, context) != revision:
                raise SettingsConflict(CONFLICT)
            # Work on a same-volume temporary copy. Native Read/Save helpers
            # jointly validate each NPC's ranges and encode its one native key.
            if len(raw) > SETTINGS_FILE_BYTES:
                raise ValueError('设置文件超过恢复容量；原配置已保留。')
            with bounded_workspace(self.path, 'settings-write', parent=self.path.parent,
                                   expected=['TheJockeyClub.ini', 'changes.tsv']) as directory:
                staged = Path(directory) / 'TheJockeyClub.ini'
                patch = Path(directory) / 'changes.tsv'
                staged.write_bytes(raw)
                patch.write_text('\n'.join(patch_lines) + '\n', encoding='utf-8')
                result = self._tool('apply', staged, context, patch)
                updated = staged.read_bytes()
                if len(updated) > SETTINGS_FILE_BYTES:
                    raise ValueError('设置文件超过恢复容量；原配置已保留。')
                if updated != raw:
                    backup_dir = self.state / 'settings-backups'
                    # Validation and flushing are outside the short F11 write
                    # exclusion. ReplaceFile also preserves any atomic editor's
                    # displaced bytes; its backup is not a prior sampled copy.
                    backup = settings_recovery_slot(backup_dir, len(raw))
                    with staged.open('r+b') as output:
                        os.fsync(output.fileno())
                    with protect_current_file(self.path):
                        current = self.path.read_bytes()
                        if current != raw or self._context(course_id) != context:
                            raise SettingsConflict(CONFLICT)
                        publish_settings(staged, self.path, backup)
                    if backup.read_bytes() != raw or self.path.read_bytes() != updated:
                        raise SettingsConflict(CONFLICT)
            return {**result, 'course_id': course_id, 'revision': self._revision(updated, context)}

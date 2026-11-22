'use strict';
(() => {
    const $ = s => document.querySelector(s),
        state = {
            catalog: null,
            course: '',
            race: '',
            mode: 'plan',
            raceFilter: 'finished',
            filterInitialized: false,
            data: null,
            token: 0,
            clearing: false,
            catalogEpoch: 0,
            algorithms: null,
            calculating: false,
            planUpdates: [],
            views: [],
            planLap: '', raceLap: 0, continuous: true
        };
    const I18n = window.RaceI18n, t = I18n.t;
    I18n.register?.({
        '对决赛': 'Duel',
        '限定赛': 'Breed challenge',
        '赛马投注': 'Race betting',
        '参赛骑手': 'Rider',
        '投注方式': 'Bet type',
        '下注骑手': 'Selected rider',
        '赌注': 'Stake',
        '结算金额': 'Settlement amount',
        '结算时待入账': 'Pending at settlement',
        '记录时点': 'Recorded at',
        '结算快照': 'Settlement snapshot',
        '选手结果': 'Rider results',
        '仅观赛': 'Spectating',
        '摔马': 'Dismounted',
        '死亡': 'Death',
        '两次出界': 'Two boundary offenses',
        '跳过赛段': 'Skipped section',
        '停滞': 'Stalled',
        '实体丢失': 'Entity lost',
        '比赛中': 'Racing',
        '出局': 'Out',
        '赛道违规': 'Course violation',
        '设定圈数': 'Scheduled laps',
        '最后在场选手': 'Last rider standing',
        '未安装': 'Not installed',
        '便携精简版未包含 SQP 的 NumPy / SciPy 依赖。': 'This slim portable package does not include the NumPy / SciPy dependencies for SQP.',
        'SQP 依赖尚未安装；参见赛事分析室 README 的候选算法安装步骤。': 'SQP dependencies are not installed; see the candidate planner setup in the diagnostics README.',
        'SQP 的 NumPy / SciPy 运行时不可用，请修复依赖后重新打开 Clubhouse。': 'The SQP NumPy / SciPy runtime is unavailable. Repair the dependencies and reopen Clubhouse.'
    });
    // Bind only authored presentation fields. Refresh text in place, leaving replay,
    // selection, camera geometry and in-flight jobs intact.
    const bindings = new Map();
    function bind(node, property, source) {
        let fields = bindings.get(node);
        if (!fields) bindings.set(node, fields = new Map());
        fields.set(property, source);
        const value = t(typeof source === 'function' ? source() : source);
        if (property.startsWith('@')) node.setAttribute(property.slice(1), value);
        else node[property] = value;
    }
    const bindText = (node, source) => bind(node, 'textContent', source);
    const bindTitle = (node, source) => bind(node, 'title', source);
    const bindAttr = (node, name, source) => bind(node, '@' + name, source);
    const localizedNode = source => { const node = document.createTextNode(''); bindText(node, source); return node; };
    const courseTitle = course => I18n.language === 'en' ? course?.title_en || t(course?.title || course?.id || '') : course?.title || course?.id || '';
    const courseOptionTitle = course => {
        const title = courseTitle(course);
        const duplicate = state.catalog?.courses.filter(item => courseTitle(item) === title).length > 1;
        const host = I18n.language === 'zh' ? course.host_name_zh || course.host_name : course.host_name_en || course.host_name;
        return duplicate && host ? title + ' · ' + host : title;
    };
    const eventKinds = {duel:'对决赛', challenge:'限定赛', bookmaker:'赛马投注'};
    const eventKind = course => Object.hasOwn(eventKinds, course?.event_kind) ? course.event_kind : '';
    const courseAccessibleTitle = course => [t(eventKinds[eventKind(course)] || ''), courseOptionTitle(course)].filter(Boolean).join(' · ');
    function eventIcon(node, course) {
        const kind = eventKind(course);
        node.className = 'event-icon' + (kind ? ' event-icon--' + kind : '');
        node.hidden = !kind;
    }
    // Native <option> cannot render a PNG reliably. Keep the select only as the
    // existing value/change bridge; the visible control owns a real listbox.
    const coursePicker = (() => {
        const select = $('#course'), button = $('#course-picker'), list = $('#course-list');
        const caption = $('#course-caption'), icon = button.querySelector('.event-icon');
        let entries = [], active = -1, typed = '', typedAt = 0;
        const isOpen = () => !list.hidden;
        function positionList() {
            const field = button.getBoundingClientRect(), sidebar = $('#sidebar').getBoundingClientRect();
            const below = Math.min(innerHeight - 8, sidebar.bottom - 8) - field.bottom - 4;
            const above = field.top - Math.max(8, sidebar.top + 8) - 4;
            const upwards = below < Math.min(180, entries.length * 36 + 2) && above > below;
            list.dataset.side = upwards ? 'above' : 'below';
            list.style.maxHeight = Math.max(36, Math.min(360, upwards ? above : below)) + 'px';
        }
        function highlight(index, reveal = true) {
            active = Math.max(0, Math.min(entries.length - 1, index));
            entries.forEach((entry, i) => entry.node.classList.toggle('active', i === active));
            const node = entries[active]?.node;
            if (isOpen() && node) {
                button.setAttribute('aria-activedescendant', node.id);
                if (reveal) {
                    const top = node.offsetTop, bottom = top + node.offsetHeight;
                    if (top < list.scrollTop) list.scrollTop = top;
                    else if (bottom > list.scrollTop + list.clientHeight) list.scrollTop = bottom - list.clientHeight;
                }
            }
        }
        function close() {
            list.hidden = true;
            button.setAttribute('aria-expanded', 'false');
            button.removeAttribute('aria-activedescendant');
            typed = '';
        }
        function sync() {
            const selected = entries.find(entry => entry.course.id === select.value)?.course;
            button.disabled = select.disabled;
            eventIcon(icon, selected);
            bindText(caption, selected ? () => courseOptionTitle(selected) : '选择赛事');
            bindTitle(button, selected ? () => courseAccessibleTitle(selected) : '选择赛事');
            // The category supplements the visible name for nonvisual browsing.
            bindAttr(button, 'aria-label', selected ? () => t('赛道') + ' · ' + courseAccessibleTitle(selected) : '选择赛事');
            button.removeAttribute('aria-labelledby');
            entries.forEach(entry => entry.node.setAttribute('aria-selected', String(entry.course.id === select.value)));
            if (button.disabled) close();
        }
        function open(index) {
            if (button.disabled || !entries.length) return;
            positionList();
            list.hidden = false;
            button.setAttribute('aria-expanded', 'true');
            highlight(index ?? Math.max(0, entries.findIndex(entry => entry.course.id === select.value)));
        }
        function choose(index) {
            const entry = entries[index];
            if (!entry) return;
            const changed = select.value !== entry.course.id;
            select.value = entry.course.id;
            close(); sync();
            button.focus({preventScroll:true});
            if (changed) select.dispatchEvent(new Event('change', {bubbles:true}));
        }
        button.onclick = () => isOpen() ? close() : open();
        button.onkeydown = event => {
            if (button.disabled || !entries.length) return;
            const opened = isOpen();
            if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
                event.preventDefault(); typed = '';
                if (!opened) open();
                else highlight(active + (event.key === 'ArrowDown' ? 1 : -1));
            } else if (event.key === 'Home' || event.key === 'End') {
                event.preventDefault(); typed = '';
                if (!opened) open(event.key === 'Home' ? 0 : entries.length - 1);
                else highlight(event.key === 'Home' ? 0 : entries.length - 1);
            } else if (event.key === 'Enter' || (event.key === ' ' && !(typed && performance.now() - typedAt <= 700))) {
                event.preventDefault();
                if (opened) choose(active); else open();
            } else if (event.key === 'Escape' && opened) {
                event.preventDefault(); event.stopPropagation(); close();
            } else if (event.key === 'Tab') {
                close();
            } else if (event.key.length === 1 && !event.ctrlKey && !event.metaKey && !event.altKey) {
                event.preventDefault();
                const now = performance.now();
                typed = now - typedAt > 700 ? event.key : typed + event.key;
                typedAt = now;
                const query = typed.toLocaleLowerCase();
                const repeated = [...query].every(letter => letter === query[0]);
                const prefix = repeated ? query[0] : query;
                const start = repeated ? active + 1 : active;
                const match = entries.findIndex((_, offset) => courseOptionTitle(entries[(start + offset + entries.length) % entries.length].course).toLocaleLowerCase().startsWith(prefix));
                if (match !== -1) {
                    const next = (start + match + entries.length) % entries.length;
                    if (!opened) open(next); else highlight(next);
                }
            }
        };
        document.addEventListener('pointerdown', event => { if (!button.parentElement.contains(event.target)) close(); });
        document.addEventListener('focusin', event => { if (!button.parentElement.contains(event.target)) close(); });
        window.addEventListener('resize', () => { if (isOpen()) positionList(); });
        $('#sidebar').addEventListener('scroll', () => { if (isOpen()) positionList(); });
        return {
            close, sync,
            setItems(courses) {
                const activeId = entries[active]?.course.id;
                for (const entry of entries) { bindings.delete(entry.label); bindings.delete(entry.node); }
                entries = courses.map((course, index) => {
                    const node = document.createElement('div');
                    node.id = 'course-option-' + index; node.className = 'course-option';
                    node.setAttribute('role', 'option');
                    const image = document.createElement('span'); image.setAttribute('aria-hidden', 'true'); eventIcon(image, course);
                    const label = document.createElement('span'); label.className = 'course-option-label';
                    bindText(label, () => courseOptionTitle(course));
                    bindAttr(node, 'aria-label', () => courseAccessibleTitle(course));
                    node.append(image, label);
                    node.onpointerdown = event => event.preventDefault(); // Focus stays on the combobox.
                    node.onclick = () => choose(index);
                    node.onpointermove = () => highlight(index, false);
                    return {course, node, label};
                });
                list.replaceChildren(...entries.map(entry => entry.node));
                sync();
                highlight(Math.max(0, entries.findIndex(entry => entry.course.id === activeId)), isOpen());
            }
        };
    })();
    const theme = RaceUI.theme;
    const palette = {player:theme.player, host:theme.host, raw:theme.survey, estimated:theme.gap, centre:theme.quiet, recordedReference:'#80bfd5'};
    const fmt = (v, d = 2) => v !== null && v !== undefined && v !== '' && Number.isFinite(Number(v)) ? Number(v).toFixed(d) : '—';
    const raceTime = value => {
        if (value == null || !Number.isFinite(value) || value < 0) return '—';
        const ms = Math.round(value * 1000);
        return `${Math.floor(ms / 60000)}:${(Math.floor(ms / 1000) % 60).toString().padStart(2, '0')}.${(ms % 1000).toString().padStart(3, '0')}`;
    };
    const dateText = v => {
        if (!v) return '时间未知';
        const d = new Date(typeof v === 'number' && v < 1e12 ? v * 1000 : v);
        return Number.isNaN(d.getTime()) ? String(v) : d.toLocaleString(I18n.locale, {
            hour12: false
        });
    };
    const raceDate = r => dateText(r.started_at && /^\d{2}:\d{2}:\d{2}$/.test(r.started_at) && r.date ? r.date + 'T' + r.started_at : r.started_at || r.modified || r.date);
    const raceEventTitle = race => {
        const course = state.catalog?.courses.find(item => item.id === race.course_id);
        return I18n.language === 'en' ? race.title_en || (course ? courseTitle(course) : t(race.title || race.host_name || '比赛')) : race.title || (course ? courseTitle(course) : race.host_name || '比赛');
    };
    const raceLabel = r => `${raceDate(r)} · ${t(r.status_label||r.status||'状态未知')} · ${t('{count} 圈',{count:r.total_laps??'—'})} · ${raceEventTitle(r)} · …${String(r.id).slice(-12)}`;

    function precisionText() {
        return '';
    }

    const status = (text, error = false) => {
        bindText($('#status'), text);
        $('#status').hidden = !text;
        $('#status').classList.toggle('error', error);
    };
    const clearHorseInfo = () => {
        const container = $('#horse-info');
        for (const section of container.children) section.destroy?.();
        container.replaceChildren();
    };
    const dispose = () => {
        // Remove presentation bindings before the owning report DOM is detached.
        // Otherwise a bound map's event handlers can retain its complete recording.
        for (const node of bindings.keys()) {
            if (!node.isConnected || ['#content', '#summary', '#horse-info'].some(selector => $(selector).contains(node))) bindings.delete(node);
        }
        state.views.forEach(v => v.destroy?.());
        state.views = [];
        const navigation = $('#lap-navigation');
        if (navigation) { navigation.replaceChildren(); navigation.hidden = true; }
        state.planUpdates = [];
        document.querySelectorAll('#algorithms .computing').forEach(button => button.classList.remove('computing'));
        hideTip();
    };
    const empty = (title, detail = '') => {
        dispose();
        $('#record-reference').hidden = true;
        $('#summary').replaceChildren();
        clearHorseInfo();
        bindText($('#source-note'), '');
        bindText($('#precision'), '');
        $('#content').replaceChildren();
        const e = document.createElement('div');
        e.className = 'empty';
        const b = document.createElement('strong');
        bindText(b, title);
        const p = document.createElement('p');
        bindText(p, detail);
        e.append(b, p);
        $('#content').append(e);
    };
    async function api(url, options) {
        const r = await fetch(url, {
            cache: 'no-store',
            ...options
        });
        let d;
        try {
            d = await r.json();
        } catch {
            throw Error(`服务返回非 JSON 内容（${r.status}）`);
        }
        if (!r.ok) {
            const error = Error(d.error || `请求失败：${r.status}`);
            error.status = r.status;
            throw error;
        }
        return d;
    }
    let settingsCapabilityChecked = false;
    async function initializeSettings() {
        if (settingsCapabilityChecked) return;
        settingsCapabilityChecked = true;
        const container = $('#global-settings');
        if (!container) return;
        const unavailable = (message, error = false) => {
            const heading = document.createElement('h2'); heading.className = 'sidebar-heading settings-heading'; heading.id = 'settings-title';
            bindText(heading,'赛事配置');
            const body = document.createElement('div'); body.className = 'settings-body';
            const note = document.createElement('p'); note.className = 'settings-status';
            if (error) { note.classList.add('error'); note.setAttribute('role','alert'); }
            bindText(note,message);body.append(note);container.replaceChildren(heading,body);
        };
        let health;
        try {
            const controller = new AbortController();
            const timer = setTimeout(() => controller.abort(),8000);
            try { health = await api('/api/health',{signal:controller.signal}); }
            finally { clearTimeout(timer); }
        } catch (error) { unavailable('无法读取本地服务能力。',true); return; }
        if (!Array.isArray(health.capabilities) || !health.capabilities.includes('global-settings')) {
            unavailable('重启本地服务后可用。');
            return;
        }
        const script = document.createElement('script'); script.src = 'settings-ui.js'; script.async = true;
        script.onload = () => {
            if (typeof window.RaceSettings?.setCourse !== 'function') { unavailable('配置界面加载失败。',true); return; }
            window.RaceSettings.setCourse(state.course);
        };
        script.onerror = () => unavailable('配置界面加载失败。',true);
        document.head.append(script);
    }
    const sleep = ms => new Promise(r => setTimeout(r, ms));
    async function job(url, options, token, onEvent) {
        let d = await api(url, options);
        const id = d.job_id;
        let revision = 0;
        while (id && d.status !== 'done' && d.status !== 'error') {
            if (token !== state.token) return null;
            for (const event of d.events || []) onEvent?.(event);
            revision = d.revision || revision;
            status(d.progress || (d.status === 'queued' ? '已排队，等待本地分析任务。' : '本地分析正在运行…'));
            await sleep(400);
            d = await api(`/api/jobs/${encodeURIComponent(id)}?after=${revision}`);
        }
        if (d.status === 'error') throw Error(d.error || '本地分析失败');
        return d.result ?? d;
    }

    function options(select, items, value, label) {
        const frag = document.createDocumentFragment();
        for (const i of items) {
            const o = document.createElement('option');
            o.value = i.id;
            bindText(o, label(i));
            frag.append(o);
        }
        select.replaceChildren(frag);
        select.value = value;
        select.disabled = !items.length;
        if (select.id === 'course') coursePicker.setItems(items);
    }

    function listedRaces(races, preserve = true) {
        const result = state.raceFilter === 'all' ? [...races] : races.filter(r => r.status === state.raceFilter);
        if (preserve && state.race && !result.some(r => r.id === state.race)) {
            const retained = races.find(r => r.id === state.race) || state.catalog?.races.find(r => r.id === state.race);
            if (retained) result.push(retained);
        }
        return result;
    }

    function controls() {
        const course = state.catalog?.courses.find(c => c.id === state.course);
        bindText($('#event-title-label'), course ? () => courseTitle(course) : '选择赛事');
        bindAttr($('#event-title'), 'aria-label', course ? () => courseAccessibleTitle(course) : '选择赛事');
        eventIcon($('#event-icon'), course);
        coursePicker.sync();
        bindText($('#event-mode'), state.mode === 'plan' ? '赛事筹备' : '比赛回看');
        document.body.dataset.mode = state.mode;
        window.RaceSettings?.setCourse(state.course);
        document.querySelectorAll('#race-filter [data-filter]').forEach(button => button.setAttribute('aria-pressed', String(button.dataset.filter === state.raceFilter)));
        bindText($('#reload'), state.mode === 'plan' ? '重新加载通道' : '重新加载记录');
        $('#plan-controls').hidden = state.mode !== 'plan';
        $('#record-controls').hidden = state.mode !== 'record';
        const reference = $('#race-reference'), referenceParent = state.mode === 'plan' ? $('#plan-controls') : $('#record-info');
        if (reference.parentNode !== referenceParent) referenceParent.prepend(reference);
        $('#plan-mode').setAttribute('aria-pressed', state.mode === 'plan');
        $('#record-mode').setAttribute('aria-pressed', state.mode === 'record');
        $('#calculate').disabled = !course?.plannable || !state.algorithms?.size || state.calculating || !!state.catalog?.planner?.busy;
        bindTitle($('#calculate'), course?.reason || (state.catalog?.planner?.busy ? '等待当前计算完成' : '在同一份通道上计算所选算法'));
    }
    async function catalog() { return refreshCatalog(false); }
    async function refreshCatalog(force = false) {
        if (state.clearing && !force) return;
        const epoch = state.catalogEpoch;
        try {
            const d = await api('/api/catalog');
            if (epoch !== state.catalogEpoch || (state.clearing && !force)) return;
            const previous = state.catalog;
            state.catalog = d;
            if (!state.algorithms) {
                state.algorithms = new Set((d.algorithms || []).filter(a => a.default && a.available !== false).map(a => a.id));
                for (const a of d.algorithms || []) {
                    const button = document.createElement('button');
                    button.type = 'button';
                    button.className = 'algorithm-choice';
                    button.dataset.algorithm = a.id;
                    button.style.setProperty('--algorithm-color', a.color);
                    button.disabled = a.available === false;
                    bindTitle(button, a.unavailable_reason || a.description);
                    button.setAttribute('aria-pressed', String(state.algorithms.has(a.id)));
                    const dot = document.createElement('i');
                    dot.setAttribute('aria-hidden','true');
                    const label = document.createElement('span'); bindText(label, a.name);
                    button.append(dot,label);
                    if (a.available === false) {
                        const unavailable = document.createElement('small');
                        unavailable.className = 'algorithm-unavailable';
                        bindText(unavailable, '未安装'); button.append(unavailable);
                    }
                    button.onclick = () => {
                        if (state.algorithms.has(a.id)) state.algorithms.delete(a.id); else state.algorithms.add(a.id);
                        button.setAttribute('aria-pressed',String(state.algorithms.has(a.id)));
                        controls();
                    };
                    $('#algorithms').append(button);
                }
            }
            const scan = d.scanner || {},
                pending = Number(scan.pending) || 0,
                invalid = Array.isArray(scan.invalid) ? scan.invalid.length : Number(scan.invalid) || 0;
            bindText($('#connection'), [scan.error ? '扫描器有错误' : scan.busy ? '扫描比赛记录中' : '', pending ? '等待 ' + pending + ' 场日志收尾' : '', invalid ? '读取失败 ' + invalid + ' 场' : ''].filter(Boolean).join(' · '));
            bindTitle($('#connection'), scan.error || [pending ? '日志尚未收尾，继续等待原文件稳定。' : '', invalid ? '部分原始日志无法读取；未将失败记录伪装为有效比赛。' : ''].filter(Boolean).join(' '));
            const courses = [...(d.courses || [])];
            if (previous && state.course && !courses.some(c => c.id === state.course)) {
                const retained = previous.courses?.find(c => c.id === state.course);
                if (retained) courses.push(retained);
            }
            if (!courses.some(c => c.id === state.course)) {
                const newest = (d.races || []).find(r => courses.some(c => c.id === r.course_id));
                state.course = newest?.course_id || courses.find(c => c.id === 'arthur-camp' || c.id === 'host/arthur-camp')?.id || courses[0]?.id || '';
                state.race = '';
            }
            if (!previous || JSON.stringify(previous.courses) !== JSON.stringify(courses)) options($('#course'), courses, state.course, courseOptionTitle);
            const candidates = (d.races || []).filter(r => !state.course || r.course_id === state.course);
            if (!state.filterInitialized) {
                state.raceFilter = candidates.some(r => r.status === 'finished') ? 'finished' : 'all';
                state.filterInitialized = true;
                document.querySelectorAll('#race-filter [data-filter]').forEach(button => button.setAttribute('aria-pressed', String(button.dataset.filter === state.raceFilter)));
            }
            const races = listedRaces(candidates);
            const oldRace = state.race;
            if (!oldRace && races.length) state.race = races[0].id;
            if (oldRace && !races.some(r => r.id === oldRace)) {
                const existing = previous?.races?.find(r => r.id === oldRace);
                if (existing) races.push(existing);
            }
            if (!previous || JSON.stringify(previous.races) !== JSON.stringify(d.races) || $('#race').value !== state.race) options($('#race'), races, state.race, raceLabel);
            if (previous) {
                const ids = new Set((previous.races || []).map(r => r.id));
                const count = races.filter(r => !ids.has(r.id)).length;
                if (count) bindText($('#new-races'), `发现 ${count} 场新记录，当前选择已保留`);
            }
            controls();
            if (scan.error) status(scan.error, true);
            if (previous && state.mode === 'record' && !oldRace && state.race && !state.data) loadRace();
            if (!previous) {
                if (state.course) loadCourse();
                else {
                    status('没有发现可分析赛道。');
                    empty('暂无赛道', '检查本地服务配置。');
                }
            }
        } catch (e) {
            if (epoch !== state.catalogEpoch || (state.clearing && !force)) return;
            bindText($('#connection'), '本地服务连接失败');
            status(e.message, true);
        }
    }
    async function loadCourse(calculate = false) {
        const token = ++state.token;
        if (!calculate) state.data = null;
        if (calculate) state.calculating = true;
        controls();
        if (!calculate || !state.data) empty(calculate ? '正在准备走线对比…' : '正在加载赛道通道…');
        status(calculate ? '冻结本次赛道输入，依次计算所选算法。' : '读取当前赛道与可利用通道。');
        try {
            const url = calculate ? '/api/compare' : `/api/course?id=${encodeURIComponent(state.course)}`;
            const d = await job(url, calculate ? {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    course_id: state.course,
                    algorithms: [...state.algorithms]
                })
            } : undefined, token, event => {
                if (token !== state.token) return;
                if (event.type === 'start') {
                    state.data = event.course;
                    renderPlan(state.data);
                } else if (state.data?.comparison) {
                    const algorithms = state.data.comparison.algorithms;
                    const id = event.id || event.candidate?.id;
                    const a = algorithms.find(a => a.id === id);
                    if (a) Object.assign(a, event.candidate || {status:event.status});
                    updateComparison();
                }
            });
            if (token !== state.token || !d) return;
            if (calculate && state.data?.comparison?.snapshot === d.comparison?.snapshot) {
                Object.assign(state.data.comparison, d.comparison);
                updateComparison();
            } else { state.data = d; renderPlan(d); }
            const candidates = d.comparison?.algorithms || [];
            const failures = candidates.filter(a => a.status !== 'done').length;
            status(!d.profiles?.length ? (d.reason || '此赛道没有固定录制路径。') : failures ? '部分候选计算失败，失败原因见结果表。' : '', !!failures);
        } catch (e) {
            if (token !== state.token) return;
            status(e.message, true);
            empty('无法加载赛道', e.message);
        } finally {
            if (calculate) { state.calculating = false; controls(); }
        }
    }
    async function loadRace() {
        const token = ++state.token;
        state.data = null;
        if (!state.race) {
            const note = state.catalog?.courses.find(c => c.id === state.course)?.record_note || '';
            empty(note ? '比赛记录说明' : '该赛道暂无比赛记录', note);
            status(note || '新记录会自动加入列表。');
            return;
        }
        empty('正在读取原始比赛采样…');
        status('解析玩家与主办人的逐圈原始记录。');
        try {
            const d = await job(`/api/races/${encodeURIComponent(state.race)}`, undefined, token);
            if (token !== state.token || !d) return;
            state.data = d;
            renderRace(d);
            status('');
        } catch (e) {
            if (token !== state.token) return;
            status(e.message, true);
            empty('无法读取比赛记录', e.message);
        }
    }

    function maintenanceBytes(bytes) {
        const units = ['B', 'KiB', 'MiB', 'GiB', 'TiB'];
        let value = bytes, unit = 0;
        while (value >= 1024 && unit < units.length - 1) { value /= 1024; unit++; }
        return (unit ? value.toFixed(1) : String(value)) + ' ' + units[unit];
    }
    async function clearRecordsAndCache() {
        if (state.clearing) return;
        state.clearing = true;
        const button = $('#maintenance-clear'), label = $('#maintenance-label'), note = $('#maintenance-status');
        button.disabled = true;
        note.classList.remove('error');
        bindText(label, '正在清理…');
        bindText(note, '');
        try {
            const result = await api('/api/maintenance/clear', {
                method: 'POST', headers: {'Content-Type': 'application/json'}, body: '{}'
            });
            if (result.ok !== true) throw Error(t('服务返回的清理结果无效。'));
            const countsValid = ['removed_files', 'bytes_reclaimed', 'skipped_files'].every(
                key => Number.isSafeInteger(result[key]) && result[key] >= 0);

            // Do not retain a deleted selection or a stale replay. The backend
            // may have skipped files; the refreshed catalog remains authoritative.
            ++state.catalogEpoch;
            ++state.token;
            state.race = ''; state.raceLap = 0; state.data = null; state.filterInitialized = false;
            if (state.catalog) state.catalog = {...state.catalog, races: []};
            options($('#race'), [], '', raceLabel);
            bindText($('#new-races'), '');
            if (state.mode === 'record') empty('该赛道暂无比赛记录');
            if (result.partial === true) note.classList.add('error');
            if (countsValid) bindText(note, () => (result.partial === true ? t('仅完成部分清理，请稍后重试。') + ' ' : '') + t('已清理 {count} 个文件，释放 {size}。', {
                count: result.removed_files, size: maintenanceBytes(result.bytes_reclaimed)
            }) + (result.skipped_files ? ' ' + t(result.skipped_files === 1 ? '1 个文件未清理。' : '{count} 个文件未清理。', {count: result.skipped_files}) : ''));
            else { note.classList.add('error'); bindText(note, '服务返回的清理结果无效。'); }
            await refreshCatalog(true);
            if (state.mode === 'record' && !state.race) await loadRace();
            else if (state.mode === 'plan') await loadCourse();
        } catch (error) {
            note.classList.add('error');
            bindText(note, error.status === 409 ? '游戏正在运行或后台任务尚未结束，请稍后再试。' :
                () => t('无法清理：{message}', {message: error.message || String(error)}));
        } finally {
            state.clearing = false;
            button.disabled = false;
            bindText(label, '清理记录与缓存');
        }
    }

    function summary(items) {
        $('#summary').replaceChildren();
        for (const [label, value] of items) {
            const metric = document.createElement('div');
            metric.className = 'metric';
            if (['比赛用时','赛道总长'].includes(label)) metric.classList.add('metric-primary');
            const term = document.createElement('dt');
            bindText(term, label);
            const number = document.createElement('dd');
            bindText(number, value);
            metric.append(term, number);
            $('#summary').append(metric);
        }
    }

    function card(title, meta) {
        const e = document.createElement('section');
        e.className = 'card lap-page';
        e.hidden = true;
        const head = document.createElement('div');
        head.className = 'card-head';
        const h = document.createElement('h2');
        bindText(h, title);
        const sub = document.createElement('span');
        sub.className = 'meta';
        bindText(sub, meta);
        head.append(h, sub);
        const canvas = document.createElement('canvas');
        canvas.className = 'map';
        canvas.tabIndex = 0;
        bindAttr(canvas, 'aria-label', title + '地图。方向键平移，加减键缩放，Home复位。');
        const note = document.createElement('div');
        note.className = 'card-note';
        const stage = document.createElement('div');
        stage.className = 'map-stage';
        stage.append(canvas);
        const map = document.createElement('div'); map.className = 'map-view'; map.append(stage);
        const chart = document.createElement('div'); chart.className = 'chart-view'; chart.append(note);
        const reset = document.createElement('button'); reset.type = 'button'; reset.className = 'map-reset';
        bindTitle(reset, '复原地图'); bindAttr(reset, 'aria-label', '复原地图');
        reset.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 10a8 8 0 1 1 1 8M4 4v6h6" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>';
        stage.append(reset);
        e.append(head, map, chart);
        $('#content').append(e);
        const result = {
            e, map, chart, reset,
            head,
            canvas,
            stage,
            note,
            layers: {corridor:true, usable:true, raw:true, estimated:true, line:true, markers:true}
        };
        const filters = document.createElement('div');
        filters.className = 'map-filters';
        const toggle = document.createElement('button');
        toggle.className = 'filter-button';
        bindAttr(toggle, 'aria-label', title + '图层');
        toggle.setAttribute('aria-expanded', 'false');
        bindTitle(toggle, '图层与标记');
        toggle.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 5h16l-6 7v6l-4 2v-8Z"/></svg>';
        const menu = document.createElement('div');
        menu.className = 'layer-menu';
        menu.hidden = true;
        bindAttr(menu, 'aria-label', title + '图层设置');
        const labels = [['corridor','赛道边界'], ['usable','可利用通道'], ['raw','通道断面'], ['estimated','估算区间'], ['line',state.mode === 'plan' ? '规划线' : '选手轨迹']];
        if (state.mode === 'record') labels.push(['markers','选手标记']);
        result.addLayer = (key, label) => {
            result.layers[key] ??= true;
            const control = document.createElement('button');
            control.className = 'layer-switch';
            control.dataset.layer = key;
            control.setAttribute('role','switch');
            control.setAttribute('aria-checked','true');
            const text = document.createElement('span');
            bindText(text, label);
            const indicator = document.createElement('i');
            indicator.className = 'switch-indicator';
            indicator.setAttribute('aria-hidden','true');
            control.append(text,indicator);
            control.onclick = () => {
                result.layers[key] = !result.layers[key];
                control.setAttribute('aria-checked',String(result.layers[key]));
                result.redraw?.();
            };
            const help = menu.querySelector('p');
            if (help) menu.insertBefore(control, help); else menu.append(control);
            return control;
        };
        for (const [key, label] of labels) result.addLayer(key, label);
        const help = document.createElement('p');
        bindText(help, '滚轮缩放 · 拖动平移 · 双击复位');
        menu.append(help);
        toggle.onclick = () => {
            menu.hidden = !menu.hidden;
            toggle.setAttribute('aria-expanded',String(!menu.hidden));
        };
        filters.onkeydown = event => {
            if (event.key === 'Escape') { menu.hidden = true; toggle.setAttribute('aria-expanded','false'); toggle.focus(); }
        };
        filters.append(toggle,menu);
        stage.append(filters);
        return result;
    }

    function pathPoints(path) {
        return (path || []).map(p => Array.isArray(p) ? {
            s: p[0],
            x: p[1],
            y: p[2],
            z: p[3]
        } : p).filter(p => Number.isFinite(p.x) && Number.isFinite(p.y));
    }
    // Clip only the two lap-edge pieces of the original planner polyline.
    // Interior native points remain untouched; this is not smoothing or sampling.
    function clipPlan(points, lo, hi) {
        if (points.length < 2 || !(hi > lo)) return [];
        const start = Math.max(lo, points[0][0]),
            end = Math.min(hi, points.at(-1)[0]);
        if (!(end > start)) return [];
        const at = s => {
            let left = 0,
                right = points.length - 1;
            while (right - left > 1) {
                const mid = (left + right) >> 1;
                if (points[mid][0] <= s) left = mid;
                else right = mid;
            }
            const a = points[left],
                b = points[right];
            if (s === a[0]) return a.slice();
            if (s === b[0]) return b.slice();
            const t = (s - a[0]) / (b[0] - a[0]);
            return [s, ...a.slice(1).map((v, i) => v + t * (b[i + 1] - v))];
        };
        return [at(start), ...points.filter(p => p[0] > start && p[0] < end), at(end)].map(p => [p[0] - lo, ...p.slice(1)]);
    }

    // A supplied core is a separate historical reference, never the canonical
    // boundary or the planner's mixed working reference_path.
    function recordedReferenceSource(data) {
        const ready = (data.comparison?.algorithms || []).filter(a => a.status === 'done');
        const sources = [data.planning, ready.find(a => a.id === 'current')?.planning, ...ready.map(a => a.planning)];
        return sources.map(p => p?.recorded_reference).find(r => r?.active === true && Array.isArray(r.core_points) && r.core_points.length >= 2) || null;
    }
    function recordedReferencePoints(reference, start, end) {
        const points = reference?.core_points;
        if (reference?.active !== true || !Array.isArray(points) || points.length < 2 ||
            points.some((p, i) => !Array.isArray(p) || p.length < 4 || !p.slice(0,4).every(Number.isFinite) || (i && p[0] <= points[i-1][0]))) return [];
        return pathPoints(clipPlan(points, start, end));
    }

    function cacheSummary(d) {
        const meta = d.live || d.meta || d.prepared || {},
            files = d.survey_files || d.meta?.survey_files || [],
            inputs = meta.input_files || d.meta?.input_files || {};
        const details = files.map(file => {
            const info = inputs[file] || Object.values(inputs).find(x => x.path === file) || {};
            const stamp = info.mtime_ns ? info.mtime_ns / 1e6 : info.modified || info.mtime;
            return `${String(file).split(/[\\/]/).at(-1)}${stamp?' · '+dateText(stamp):''}`;
        });
        const planning = d.planning;
        const diagnostic = planning ? [
            ['本次诊断速度／加速度', `${Number.isFinite(planning.raw_speed)?planning.raw_speed+1:'—'} / ${Number.isFinite(planning.raw_acceleration)?planning.raw_acceleration+1:'—'}`],
            ['横向参数', fmt(planning.lateral)],
            ['线路利用率', Number.isFinite(planning.aggression) ? fmt(planning.aggression * 100, 0) + '%' : '—']
        ] : [];
        return [
            ['实际比赛圈数', meta.actual_laps ?? meta.total_laps ?? '—'],
            ['通道资料', details.join(t('；')) || '无匹配数据'], ...diagnostic
        ];
    }

    function tip(text, event) {
        const el = $('#tooltip');
        bindText(el, text);
        el.hidden = false;
        const rect = el.getBoundingClientRect();
        el.style.left = `${Math.max(8,Math.min(innerWidth-rect.width-8,event.clientX+14))}px`;
        el.style.top = `${Math.max(8,Math.min(innerHeight-rect.height-8,event.clientY+14))}px`;
    }

    function hideTip() {
        $('#tooltip').hidden = true;
    }

    function spans(v) {
        return v?.length ? v.map(p => t('{low} ～ {high}', {low:fmt(p[0]), high:fmt(p[1])})).join(t('；')) : t('无');
    }

    function corridorStatus(d) {
        if (String(d.survey_source || '').startsWith('historical-')) return '历史勘探快照';
        const caches = d.profile_caches || [];
        if (!caches.length) return '未提供原生基底';
        const native = caches.filter(profile => profile.native_loaded).length;
        const physical = caches.filter(profile => profile.evidence_loaded && profile.physical_rows > 0).length;
        const rejected = caches.some(profile => profile.load_error);
        return `原生基底 ${native}/${caches.length} 圈型 · ${physical ? '已叠加实际物理修正' : '暂无实际物理修正'}${rejected ? ' · 部分数据未通过校验' : ''}`;
    }

    function comparisonTable(comparison) {
        const section = document.createElement('section');
        section.className = 'comparison-results';
        const head = document.createElement('div'); head.className = 'comparison-heading';
        const title = document.createElement('h2'); bindText(title, '候选走线');
        const stamp = document.createElement('span');
        bindText(stamp, () => `${dateText(comparison.created)} · ${comparison.snapshot.slice(0,8)}`);
        bindTitle(stamp, '本次输入快照：' + comparison.snapshot);
        head.append(title,stamp);
        const scroll = document.createElement('div'); scroll.className = 'comparison-scroll';
        const table = document.createElement('table');
        const header = document.createElement('tr');
        for (const text of ['算法','线路长度','最大曲率','计算耗时','校验后回退']) {
            const th = document.createElement('th'); bindText(th, text); header.append(th);
        }
        const thead = document.createElement('thead'); thead.append(header); table.append(thead);
        const body = document.createElement('tbody');
        for (const a of comparison.algorithms) {
            const row = document.createElement('tr');
            const name = document.createElement('th'); name.scope = 'row'; name.className = 'algorithm-name';
            name.style.setProperty('--algorithm-color',a.color); bindText(name, a.name); bindTitle(name, a.description);
            row.append(name);
            if (a.status !== 'done') {
                const cell = document.createElement('td'); cell.colSpan = 4;
                cell.className = a.status === 'error' ? 'candidate-error' : 'candidate-pending';
                if (a.status === 'running') { const spinner=document.createElement('i'); spinner.className='spinner'; spinner.setAttribute('aria-hidden','true'); cell.append(spinner); }
                cell.append(localizedNode(a.status === 'error' ? a.error : a.status === 'running' ? '正在计算…' : '等待计算'));
                row.append(cell);
            } else {
                const ms = a.solver_ms;
                const cells = [`${fmt(a.metrics.length_m,1)} m`, `${fmt(a.metrics.peak_curvature,4)} m⁻¹`,
                    `${ms >= 1000 ? fmt(ms/1000,2)+' s' : fmt(ms,1)+' ms'}${a.cached ? ' · 已缓存' : ''}`,
                    `${a.planning.recorded_segments}/${a.planning.total_segments} 段`];
                for (const text of cells) { const td = document.createElement('td'); bindText(td, text); row.append(td); }
                bindTitle(row.children[3], `离线求解耗时；含 SQP 初始线、求解及最终校验。首次进程总耗时 ${fmt(a.wall_ms/1000,2)} 秒。`);
                bindTitle(row.children[4], a.planning.obstacle_verified ? '全部通过通道检查' : '缺少完整通道证据的区段；已检查已知障碍与赛道边界。');
            }
            body.append(row);
        }
        table.append(body); scroll.append(table);
        const note = document.createElement('p'); note.className = 'comparison-note';
        bindText(note, '曲率越低，线路越平顺。耗时为离线求解，不代表游戏帧耗时；回退段数可用于辨别校验对走线的影响。');
        section.append(head,scroll,note); $('#content').prepend(section);
    }

    function updateComparison() {
        const comparison = state.data?.comparison;
        if (!comparison) return;
        const previousTable = $('.comparison-results');
        if (previousTable) {
            for (const node of bindings.keys()) if (previousTable.contains(node)) bindings.delete(node);
            previousTable.remove();
        }
        comparisonTable(comparison);
        for (const update of state.planUpdates) update();
        for (const a of comparison.algorithms) {
            const button = document.querySelector(`#algorithms [data-algorithm="${a.id}"]`);
            button?.classList.toggle('computing', a.status === 'running');
        }
        const checks = [...$('#summary').children].find(item => item.querySelector('dt')?.textContent === t('路线检查'));
        if (checks) checks.querySelector('dd').textContent = `${comparison.algorithms.filter(a => a.status === 'done').length} 个候选可比较`;
    }

    function lapNavigation(pages, initial, onSelect) {
        const navigation = $('#lap-navigation');
        navigation.replaceChildren();
        navigation.hidden = !pages.length;
        navigation.classList.add('lap-navigation-shell');
        navigation.setAttribute('role','group');
        bindAttr(navigation,'aria-label','圈次');
        const strip = document.createElement('div'); strip.className = 'lap-tabs-scroll';
        strip.setAttribute('role','tablist'); bindAttr(strip,'aria-label','圈次');
        const arrows = [-1,1].map(direction => {
            const button = document.createElement('button'); button.type='button';
            button.className = 'lap-scroll-button '+(direction<0?'lap-scroll-prev':'lap-scroll-next');
            button.dataset.direction=String(direction);
            const label=direction<0?'向前滚动圈次':'向后滚动圈次';
            bindTitle(button,label); bindAttr(button,'aria-label',label);
            button.innerHTML='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="'+(direction<0?'m15 5-7 7 7 7':'m9 5 7 7-7 7')+'" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>';
            button.onclick=()=>strip.scrollBy({left:direction*Math.max(120,strip.clientWidth*.8),behavior:'smooth'});
            return button;
        });
        navigation.append(strip,...arrows);
        function updateOverflow() {
            const left=strip.scrollLeft>1,right=strip.scrollLeft+strip.clientWidth<strip.scrollWidth-1;
            navigation.dataset.overflowLeft=String(left); navigation.dataset.overflowRight=String(right);
            arrows[0].disabled=!left;arrows[1].disabled=!right;
        }
        function reveal(index) {
            const button=tabs[index]; if(!button)return;
            const outer=strip.getBoundingClientRect(),inner=button.getBoundingClientRect();
            const left=inner.left-outer.left+strip.scrollLeft,right=left+inner.width;
            if(left<strip.scrollLeft)strip.scrollTo({left:Math.max(0,left-4),behavior:'smooth'});
            else if(right>strip.scrollLeft+strip.clientWidth)strip.scrollTo({left:right-strip.clientWidth+4,behavior:'smooth'});
            updateOverflow();
        }
        const tabs = pages.map((page,index) => {
            const button = document.createElement('button'); button.type = 'button';
            button.dataset.lapId = page.id; button.setAttribute('role','tab');
            button.setAttribute('aria-controls',page.card.e.id);
            bindText(button,page.label);
            button.onclick = () => select(index);
            button.onkeydown = event => {
                const next = event.key==='ArrowRight' ? (index+1)%pages.length : event.key==='ArrowLeft' ? (index+pages.length-1)%pages.length : event.key==='Home' ? 0 : event.key==='End' ? pages.length-1 : null;
                if (next===null) return;
                event.preventDefault(); select(next); tabs[next].focus({preventScroll:true});
            };
            strip.append(button);
            return button;
        });
        let current = -1;
        function select(index, automatic = false, snapshot = null) {
            if (index < 0 || index >= pages.length || index === current) return;
            const previous = current;
            const preferences = snapshot || (previous >= 0 ? pages[previous].replay?.capture() : null);
            if (previous >= 0) { pages[previous].replay?.setActive(false); pages[previous].card.e.hidden = true; }
            current = index;
            pages.forEach((page,i) => { tabs[i].setAttribute('aria-selected',String(i===index)); tabs[i].tabIndex = i===index?0:-1; });
            const page = pages[index]; page.card.e.hidden = false;
            page.viewer.refresh();
            page.replay?.setActive(true);
            if (preferences && page.replay) {
                const localTime = page.replay.capture().time;
                page.replay.restore({...preferences,time:localTime}, {atStart:automatic,resume:preferences.playing});
            } else page.replay?.draw();
            reveal(index);
            onSelect?.(index);
        }
        strip.addEventListener('scroll',updateOverflow,{passive:true});
        const overflowObserver = new ResizeObserver(() => { updateOverflow(); reveal(current); });
        overflowObserver.observe(strip);
        state.views.push({destroy(){overflowObserver.disconnect();strip.removeEventListener('scroll',updateOverflow);}});
        select(Math.max(0,Math.min(pages.length-1,initial)));
        updateOverflow();
        return {select, get current() { return current; }, navigation};
    }

    function renderPlan(d) {
        dispose();
        clearHorseInfo();
        $('#record-reference').hidden = true;
        $('#content').replaceChildren();
        const params = d.parameters || {};
        summary([
            ['赛道总长', `${fmt(d.length)} m`],
            ['赛道宽度', `${fmt(Number(params.half_width) * 2)} m`],
            ['安全余量（每侧）', `${fmt(params.margin)} m`],
            ['马匹半宽', `${fmt(params.obstacle_clearance)} m`],
            ['路线检查', d.comparison ? `${d.comparison.algorithms.filter(a => a.status === 'done').length} 个候选可比较` : !d.planning ? '待生成路线' : d.planning.obstacle_verified ? '通过' : '部分区间待验证']
        ]);
        if (!d.profiles?.length) {
            empty('没有固定录制路径', d.source_note || d.reason || state.catalog?.courses.find(c => c.id === state.course)?.reason || '');
            return;
        }
        bindText($('#precision'), precisionText(d.precision || state.catalog?.precision));
        bindText($('#source-note'), '');
        if (d.comparison) comparisonTable(d.comparison);
        const pages = [];
        for (const p of d.profiles) {
            const observations = p.observations || [];
            const c = card(({
                first: '首圈',
                regular: '常规圈',
                final: '末圈'
            })[p.id] || p.id, `${fmt(p.length)} m`);
            const referenceControl = c.addLayer('recordedReference', '原始录制线');
            bindTitle(referenceControl, '仅显示已提供的原始录制区段。');
            referenceControl.querySelector('span').style.color = palette.recordedReference;
            let obs = observations.reduce((a, b) => (b.rows || []).filter(r => r.detailed).length > (a.rows || []).filter(r => r.detailed).length ? b : a, observations[0] || {});
            const candidates = d.comparison?.algorithms || [];
            const candidateButtons = new Map();
            if (candidates.length) {
                const legend = document.createElement('div'); legend.className = 'candidate-legend';
                bindAttr(legend, 'aria-label', `${p.id} 候选走线显隐`);
                for (const a of candidates) {
                    const key = 'candidate-' + a.id;
                    c.layers[key] = true;
                    const b = document.createElement('button'); b.type = 'button'; b.className = 'candidate-toggle';
                    b.dataset.candidate = a.id; b.style.setProperty('--algorithm-color',a.color);
                    b.setAttribute('aria-pressed','true'); bindText(b, a.name);
                    b.disabled = a.status !== 'done';
                    candidateButtons.set(a.id,b);
                    b.onclick = () => { c.layers[key] = !c.layers[key]; b.setAttribute('aria-pressed',String(c.layers[key])); c.redraw?.(); };
                    legend.append(b);
                }
                c.head.append(legend);
            }
            let model;
            const update = () => {
                const start = Number(obs.global_start ?? 0);
                const end = Number(obs.global_end ?? start + p.length);
                model = {
                    path: pathPoints(p.path),
                    rows: obs.rows || [],
                    fixedObstacles: p.fixed_obstacles || [],
                    params,
                    series: [],
                    planning: d.planning ? pathPoints(clipPlan(d.planning.points || [], start, end)) : [],
                    recordedReference: recordedReferencePoints(recordedReferenceSource(d), start, end),
                    candidates: (d.comparison?.algorithms || []).filter(a => a.status === 'done').map(a => ({id:a.id, name:a.name, color:a.color,
                        points:pathPoints(clipPlan(a.planning.points, start, end))})),
                    length: p.length,
                    title: p.id
                };
                referenceControl.hidden = model.recordedReference.length < 2;
                c.note.hidden = true;
                if (model.redraw) model.redraw();
            };
            update();
            const viewer = mapView(c.canvas, () => model, c.layers);
            c.redraw = viewer.draw;
            c.reset.onclick = viewer.reset;
            c.e.id = 'profile-'+p.id; c.e.classList.add('planning-page'); c.chart.hidden = true;
            pages.push({id:p.id,label:({first:'首圈',regular:'常规圈',final:'末圈'})[p.id] || p.id,card:c,viewer});
            state.views.push(viewer);
            let readySignature = model.candidates.map(a => a.id).join('|');
            let readyReference = recordedReferenceSource(d), readyReferencePoints = readyReference?.core_points;
            state.planUpdates.push(() => {
                for (const a of d.comparison?.algorithms || []) {
                    const b = candidateButtons.get(a.id);
                    if (b) { b.disabled = a.status !== 'done'; b.classList.toggle('computing',a.status === 'running'); }
                }
                const ready = (d.comparison?.algorithms || []).filter(a => a.status === 'done');
                const signature = ready.map(a => a.id).join('|');
                const reference = recordedReferenceSource(d);
                if (signature !== readySignature || reference !== readyReference || reference?.core_points !== readyReferencePoints) {
                    const start = Number(obs.global_start ?? 0), end = Number(obs.global_end ?? start + p.length);
                    model.candidates = ready.map(a => ({id:a.id,name:a.name,color:a.color,points:pathPoints(clipPlan(a.planning.points,start,end))}));
                    model.recordedReference = recordedReferencePoints(reference, start, end);
                    referenceControl.hidden = model.recordedReference.length < 2;
                    readyReference = reference; readyReferencePoints = reference?.core_points;
                    readySignature = signature;
                    viewer.draw();
                }
            });
        }
        lapNavigation(pages,Math.max(0,pages.findIndex(page => page.id===state.planLap)),index => { state.planLap=pages[index].id; });
    }

    function renderRace(d) {
        dispose();
        $('#content').replaceChildren();
        const metadata = d.metadata || {};
        const item = state.catalog?.races.find(r => r.id === state.race) || {};
        const betting = metadata.kind === 'bookmaker' ? metadata.bookmaker : null;
        const bettingResult = metadata.bookmaker_result || {};
        const raceSummary = [
            ['赛事日期', () => raceDate({
                ...item,
                ...metadata
            })],
            ['比赛结果', metadata.outcome || metadata.result || item.status_label || '—'],
            [betting ? '参赛骑手' : '主办人', d.host_name || '—'],
            [betting ? '设定圈数' : '实际圈数', betting ? betting.laps ?? '—' : (d.laps || []).filter(l => l.lap != null).length],
        ];
        if (betting) {
            const entrants = betting.entrants || [];
            const name = e => I18n.language === 'en' ? e?.name || e?.name_zh : e?.name_zh || e?.name;
            const selected = entrants.find(e => e.selected);
            raceSummary.push(['投注方式', betting.wagered ? (betting.market === 'win' ? 'Win' : 'Place') : '仅观赛']);
            if (betting.wagered) {
                raceSummary.push(['下注骑手', name(selected) || '—']);
                raceSummary.push(['赌注', '$' + (Number(betting.stake_cents) / 100).toFixed(2)]);
                if (metadata.bookmaker_result) raceSummary.push(['记录时点', '结算快照']);
                if (Number.isFinite(bettingResult.settlement_credit_cents))
                    raceSummary.push([bettingResult.credit_state === 'pending' ? '结算时待入账' : '结算金额',
                        '$' + (bettingResult.settlement_credit_cents / 100).toFixed(2)]);
            }
            const resultRows = (bettingResult.entrants || []).map(r => {
                const entry = entrants.find(e => e.index === r.index);
                const title = name(entry) || `#${r.index + 1}`;
                const reason = {'dismounted':'摔马', 'death':'死亡', 'second boundary offense':'两次出界',
                    'skipped-section':'跳过赛段', 'stalled':'停滞', 'entity lost':'实体丢失', 'route violation':'赛道违规'}[r.out_reason]
                    || r.out_reason || (r.status === 'racing' ? '比赛中' : '出局');
                return r.place > 0 ? `${r.place}. ${title}${r.survival_win ? ` (${t('最后在场选手')})` : ''}` : `${title} (${t(reason)})`;
            });
            if (resultRows.length) raceSummary.push(['选手结果', resultRows.join(' · ')]);
        } else {
            raceSummary.push(['比赛用时', raceTime(d.statistics?.duration_s)]);
            raceSummary.push(['玩家最高速度', fmt(d.statistics?.peak_speed_kmh, 1) + ' km/h']);
            raceSummary.push(['玩家平均速度', fmt(d.statistics?.average_speed_kmh, 1) + ' km/h']);
        }
        summary(raceSummary);
        bindText($('#precision'), '');
        bindText($('#source-note'), '');
        clearHorseInfo();
        HorseUI.horses($('#horse-info'), d);
        $('#record-reference').hidden = false;
        bindText($('#record-id'), metadata.id || metadata.record_id || state.race);
        clearTimeout(copyStatusTimer);
        bindText($('#record-copy-status'), '');
        const pages = [];
        let tabs;
        let lapIndex = 0;
        for (const lap of d.laps || []) {
            const c = card(lap.lap == null ? '圈次未确定' : `第 ${lap.lap} 圈`,
                ({
                    first: '首圈',
                    regular: '常规圈',
                    final: '末圈'
                })[lap.profile] || '比赛回看');
            const duration = lap.statistics?.duration_s;
            const entered = Number.isFinite(lap.replay?.start_t);
            let lapState = '未进入';
            if (entered) {
                const seconds = Number.isFinite(duration) ? duration : Math.max(0, lap.replay.end_t - lap.replay.start_t);
                lapState = `${raceTime(seconds)}${lap.replay.complete === false ? ' · 未完成' : ''}`;
            }
            const lapTiming = document.createElement('span');
            lapTiming.className = 'lap-timing';
            bindText(lapTiming, lapState);
            c.head.append(lapTiming);
            c.e.id = `lap-${++lapIndex}`;
            const series = [{
                key: 'player',
                name: '玩家',
                points: lap.player || []
            }, {
                key: 'host',
                name: d.host_name || '主办人',
                points: lap.host || []
            }];
            if (betting) for (const [key, points] of Object.entries(lap.riders || {})) {
                const index = Number(key.slice('jockey-'.length));
                const entry = (betting.entrants || []).find(e => e.index === index);
                const name = I18n.language === 'en' ? entry?.name || entry?.name_zh : entry?.name_zh || entry?.name;
                series.push({key, name: name || key, points});
            }
            const model = {
                path: pathPoints(lap.path),
                rows: lap.rows || [],
                params: d.course?.parameters || {},
                series,
                planning: [],
                length: Number(lap.end) - Number(lap.start),
                markers: []
            };
            const viewer = mapView(c.canvas, () => model, c.layers);
            c.redraw = viewer.draw;
            state.views.push(viewer);
            c.reset.onclick = viewer.reset;
            const pageIndex = pages.length;
            const replay = RaceUI.replay(c, lap, series, model, viewer, {managed:true,getContinuous:()=>state.continuous,setContinuous:value=>{state.continuous=value;},onEnd(snapshot) {
                if (!state.continuous || !tabs || tabs.current!==pageIndex) return;
                const next=pages[pageIndex+1];
                if (next?.replay.available) tabs.select(pageIndex+1,true,snapshot);
            }});
            pages.push({id:c.e.id,label:lap.lap==null?'未定圈次':`第 ${lap.lap} 圈`,card:c,viewer,replay});
            state.views.push(replay);
            c.note.replaceChildren();
            for (const [key, text] of [['player','玩家'],['host',d.host_name || '主办人'],['boost','速度爆发'],['stimulant','玩家兴奋剂'],['gap','玩家领先距离 · 右轴']]) {
                const item = document.createElement('span');
                item.className = `series-label series-${key}`;
                bindText(item, text);
                if (key === 'stimulant' && metadata.stimulant_timing === 'second') bindTitle(item, '药效时间由本场秒级日志还原');
                c.note.append(item);
            }
        }
        const hashIndex = /^#lap-(\d+)$/.exec(location.hash);
        tabs = lapNavigation(pages,hashIndex ? Number(hashIndex[1])-1 : state.raceLap,index => {
            state.raceLap=index;
            history.replaceState(null,'','#lap-'+(index+1));
        });
    }

    function mapView(canvas, getModel, layers) {
        const ctx = canvas.getContext('2d');
        const resolveMapFont = () => '12px ' + (getComputedStyle(canvas).getPropertyValue('--sans').trim() || 'system-ui');
        let mapFont = resolveMapFont();
        let w = 1,
            h = 1,
            cx = 0,
            cy = 0,
            scale = 1,
            drag = null,
            hover = null,
            staticCache = null,
            cameraTarget = null,
            bounds = null, fitScale = 1;
        const paths = new WeakMap(), usableGeometry = new WeakMap(), crosses = new WeakMap();

        function fit() {
            const m = getModel();
            let minX = Infinity,
                maxX = -Infinity,
                minY = Infinity,
                maxY = -Infinity;
            for (const points of [m.path, ...m.series.map(s=>s.points), m.planning]) for (const p of points) {
                minX = Math.min(minX, p.x);
                maxX = Math.max(maxX, p.x);
                minY = Math.min(minY, p.y);
                maxY = Math.max(maxY, p.y);
            }
            if (!Number.isFinite(minX)) { draw(); return; }
            bounds = {minX,maxX,minY,maxY};
            cx = (minX + maxX) / 2;
            cy = (minY + maxY) / 2;
            fitScale = Math.max(.005,Math.min((w - 56) / Math.max(20, maxX - minX), (h - 56) / Math.max(20, maxY - minY)));
            scale = fitScale;
            draw();
        }
        function constrainCamera() {
            scale = Math.max(Math.max(.005,fitScale*.5),Math.min(1000,scale));
            if (!bounds || cameraTarget) return;
            const padX=Math.max(0,w/2-48)/scale, padY=Math.max(0,h/2-48)/scale;
            cx=Math.max(bounds.minX-padX,Math.min(bounds.maxX+padX,cx));
            cy=Math.max(bounds.minY-padY,Math.min(bounds.maxY+padY,cy));
        }
        const xy = p => [w / 2 + (p.x - cx) * scale, h / 2 - (p.y - cy) * scale];

        function retainedPath(points, connected = false, closed = false) {
            if (!connected && !closed && paths.has(points)) return paths.get(points);
            const path = new Path2D();
            let previous = null;
            for (const p of points) {
                if (!previous || (!connected && (p.chunk !== previous.chunk || p.entity_id !== previous.entity_id || (Number.isFinite(p.t) && Number.isFinite(previous.t) && (p.t <= previous.t || p.t - previous.t > 0.3 + 1e-9))))) path.moveTo(p.x,-p.y);
                else path.lineTo(p.x,-p.y);
                previous = p;
            }
            if (closed) path.closePath();
            if (!connected && !closed) paths.set(points,path);
            return path;
        }
        function worldTransform(g, frame) {
            g.translate(frame.width/2-frame.cx*scale,frame.height/2+frame.cy*scale);
            g.scale(scale,scale);
        }
        function stroke(points, color, width = 1, dash = [], g = ctx, frame = {width:w,height:h,cx,cy}) {
            g.save();worldTransform(g,frame);
            g.strokeStyle = color;g.lineWidth = width/scale;
            g.lineCap = g.lineJoin = 'round';g.setLineDash(dash.map(value=>value/scale));
            g.stroke(retainedPath(points));g.restore();
        }

        function cross(row, interval, color, width = 1, dash = [], g = ctx, frame) {
            if (!interval || !Number.isFinite(row.nx) || !Number.isFinite(row.ny)) return;
            let intervals=crosses.get(row);
            if(!intervals){intervals=new WeakMap();crosses.set(row,intervals);}
            let points=intervals.get(interval);
            if(!points){
                points=[{x:row.x+row.nx*interval[0],y:row.y+row.ny*interval[0]},
                    {x:row.x+row.nx*interval[1],y:row.y+row.ny*interval[1]}];
                intervals.set(interval,points);
            }
            stroke(points,color,width,dash,g,frame);
        }

        function prepareUsable(m) {
            const commands = [];
            const command = (points, color, width, dash, fill = false) => {
                let minX=Infinity,maxX=-Infinity,minY=Infinity,maxY=-Infinity;
                for(const point of points){minX=Math.min(minX,point.x);maxX=Math.max(maxX,point.x);minY=Math.min(minY,point.y);maxY=Math.max(maxY,point.y);}
                commands.push({path:retainedPath(points,true,fill),color,width,dash,fill,minX,maxX,minY,maxY});
            };
            const line = (points, color, width, dash = []) => command(points,color,width,dash);
            const at = (r, l) => ({
                x: r.x + r.nx * l,
                y: r.y + r.ny * l
            });
            const edge = (a, b, lo, hi) => Array.from({
                length: 17
            }, (_, i) => {
                const t = i / 16,
                    l = lo + t * (hi - lo);
                return {
                    x: a.x + t * (b.x - a.x) + l * (a.nx + t * (b.nx - a.nx)),
                    y: a.y + t * (b.y - a.y) + l * (a.ny + t * (b.ny - a.ny))
                };
            });
            const intersect = (a, b) => {
                const result = [];
                for (const x of a || [])
                    for (const y of b || []) {
                        const lo = Math.max(x[0], y[0]),
                            hi = Math.min(x[1], y[1]);
                        if (lo <= hi) result.push([lo, hi]);
                    }
                return result;
            };
            const band = (a, b, span, estimated) => {
                const color = estimated ? palette.estimated : palette.raw;
                const lower = edge(a, b, span[0], span[2]),
                    upper = edge(a, b, span[1], span[3]),
                    poly = [...lower, ...upper.slice().reverse()];
                command(poly,estimated ? theme.gap + '30' : theme.survey + '24',0,[],true);
                line(lower, color, 1.1, estimated ? [3, 3] : []);
                line(upper, color, 1.1, estimated ? [3, 3] : []);
            };
            const explicit = m.rows.some(r => r.available_strips !== undefined || r.usable_strips !== undefined);
            for (let i = 1; i < m.rows.length; i++) {
                const a = m.rows[i - 1],
                    b = m.rows[i];
                if (a.index !== undefined && b.index !== a.index + 1) continue;
                if (b.available_strips !== undefined) {
                    if (layers.usable)
                        for (const span of b.available_strips) band(a, b, span, false);
                    if (layers.estimated)
                        for (const span of b.estimate_strips || []) band(a, b, span, true);
                } else if (explicit) {
                    const estimated = !!b.estimated;
                    if (estimated ? layers.estimated : layers.usable)
                        for (const span of b.usable_strips || []) band(a, b, span, estimated);
                } else {
                    if (layers.usable)
                        for (const span of intersect(intersect(b.link, a.walk), b.walk)) band(a, b, [...span, ...span], false);
                    if (layers.estimated && b.estimate_link?.length) {
                        const strips = b.estimate_strips || intersect(intersect(b.estimate_link, a.estimate_walk?.length ? a.estimate_walk : a.walk), b.estimate_walk?.length ? b.estimate_walk : b.walk).map(span => [...span, ...span]);
                        for (const span of strips) band(a, b, span, true);
                    }
                }
            }
            for (const r of m.rows) {
                if (r.available !== undefined) {
                    if (layers.usable)
                        for (const span of r.available) line([at(r, span[0]), at(r, span[1])], palette.raw, .7);
                    if (layers.estimated)
                        for (const span of r.estimate_walk || []) line([at(r, span[0]), at(r, span[1])], palette.estimated, .7, [2, 2]);
                    continue;
                }
                const estimated = !!r.estimated;
                if (explicit && (estimated ? layers.estimated : layers.usable))
                    for (const span of r.usable || []) line([at(r, span[0]), at(r, span[1])], estimated ? palette.estimated : palette.raw, .7, estimated ? [2, 2] : []);
            }
            return commands;
        }
        function usableLayer(m, target, frame) {
            if (!m.path.length || !(m.params.usable_half_width > 0)) return;
            const geometryKey = `${layers.usable}|${layers.estimated}`;
            let prepared = usableGeometry.get(m);
            if (!prepared || prepared.rows !== m.rows || prepared.key !== geometryKey) {
                prepared = {rows:m.rows,key:geometryKey,commands:prepareUsable(m)};
                usableGeometry.set(m,prepared);
            }
            const layer=document.createElement('canvas'),dpr=devicePixelRatio||1;
            layer.width=Math.round(frame.width*dpr);layer.height=Math.round(frame.height*dpr);
            const g=layer.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);
            g.save();worldTransform(g,frame);g.lineJoin=g.lineCap='round';
            const halfX=frame.width/2/scale,halfY=frame.height/2/scale,pad=2/scale;
            for(const op of prepared.commands){
                if(op.maxX<frame.cx-halfX-pad||op.minX>frame.cx+halfX+pad||op.maxY<frame.cy-halfY-pad||op.minY>frame.cy+halfY+pad)continue;
                if(op.fill){g.fillStyle=op.color;g.fill(op.path);}
                else{g.strokeStyle=op.color;g.lineWidth=op.width/scale;g.setLineDash(op.dash.map(value=>value/scale));g.stroke(op.path);}
            }
            g.restore();
            // Clip in world geometry by stroking the original recorded path. Do not
            // subtract a scalar margin a second time from section coordinates.
            g.globalCompositeOperation = 'destination-in';
            stroke(m.path, '#fff', 2 * m.params.usable_half_width * scale, [], g, frame);
            g.globalCompositeOperation = 'source-over';
            target.drawImage(layer,0,0,frame.width,frame.height);
        }

        const drawable = () => !destroyed && w >= 2 && h >= 2 &&
            canvas.width > 0 && canvas.height > 0 && !canvas.closest('.lap-page')?.hidden;
        function draw() {
            if (!drawable()) return;
            constrainCamera();
            if (cameraTarget) { cx = cameraTarget.x; cy = cameraTarget.y; }
            canvas.dataset.camera = JSON.stringify({x:cx,y:cy,scale});
            const m = getModel();
            ctx.save();
            ctx.setTransform(1, 0, 0, 1, 0, 0);
            ctx.clearRect(0, 0, canvas.width, canvas.height);
            ctx.fillStyle = theme.map;
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            ctx.restore();
            const cacheKey = [scale,w,h,canvas.width,canvas.height,...Object.values(layers)].join('|');
            const offset = cached => ({x:(w-cached.width)/2+(cached.cx-cx)*scale,y:(h-cached.height)/2+(cy-cached.cy)*scale});
            const inside = cached => {
                const p=offset(cached);
                return p.x<=1e-6&&p.y<=1e-6&&p.x+cached.width>=w-1e-6&&p.y+cached.height>=h-1e-6;
            };
            if (!(staticCache?.model === m && staticCache.key === cacheKey && staticCache.series === m.series && staticCache.candidates === m.candidates && staticCache.recordedReference === m.recordedReference && inside(staticCache))) {
                // Pan within a bounded overscan surface. A following camera no
                // longer invalidates all static geometry on every animation tick.
                const dpr=devicePixelRatio||1;
                const margin=Math.floor(Math.min(256,Math.min(w,h)/2,750000/(dpr*dpr*(w+h)))*dpr)/dpr;
                const image=document.createElement('canvas');
                image.width=canvas.width+Math.round(2*margin*dpr);image.height=canvas.height+Math.round(2*margin*dpr);
                const frame={width:image.width/dpr,height:image.height/dpr,cx,cy},g=image.getContext('2d');
                g.setTransform(dpr,0,0,dpr,0,0);
                if (layers.corridor && m.path.length) {
                    stroke(m.path,theme.muted+'20',(m.params.half_width||0)*2*scale,[],g,frame);
                }
                usableLayer(m,g,frame);
                if (layers.usable) for (const obstacle of m.fixedObstacles || []) {
                    const x=frame.width/2+(obstacle.x-frame.cx)*scale;
                    const y=frame.height/2-(obstacle.y-frame.cy)*scale;
                    const radius=Math.max(3,obstacle.radius*scale);
                    g.beginPath();g.arc(x,y,radius,0,Math.PI*2);
                    g.fillStyle='#b6382d';g.fill();g.strokeStyle='#f2d8bb';g.lineWidth=1;g.stroke();
                }
                stroke(m.path,palette.centre,1,[3,4],g,frame);
                for (const row of m.rows) {
                    if (layers.raw)
                        for (const v of row.raw || []) cross(row,v,theme.survey+'60',1,[],g,frame);
                    if (layers.estimated) {
                        for (const v of row.estimate_walk || []) cross(row,v,palette.estimated,1.5,[3,3],g,frame);
                        for (const v of row.estimate_link || []) cross(row,v,theme.gap+'55',1,[1,4],g,frame);
                    }
                }
                if (layers.line) {
                    stroke(m.planning,palette.player,2,[],g,frame);
                    for (const a of m.candidates || []) if (layers['candidate-'+a.id]) stroke(a.points,a.color,2.2,[],g,frame);
                    for (const s of m.series) stroke(s.points,palette[s.key]||theme.muted,1.7,s.key==='host'?[5,3]:[],g,frame);
                }
                if (layers.recordedReference && m.recordedReference?.length)
                    stroke(m.recordedReference,palette.recordedReference,2,[6,3],g,frame);
                staticCache = {
                    model: m,
                    key: cacheKey,
                    series: m.series,
                    candidates: m.candidates,
                    recordedReference: m.recordedReference,
                    image,...frame
                };
            }
            const p=offset(staticCache);
            ctx.drawImage(staticCache.image,p.x,p.y,staticCache.width,staticCache.height);
            if (layers.markers) for (const marker of m.markers || []) {
                const [mx, my] = xy(marker);
                RaceUI.drawMarker(ctx, marker, mx, my);
            }
            const hoverVisible = hover && (!hover.candidateId || (layers.line && layers['candidate-'+hover.candidateId])) && (!hover.actorId || (layers.markers && m.series.some(s => (s.id || s.key) === hover.actorId)));
            canvas.dataset.highlightActor = hoverVisible ? hover.actorId || 'course' : '';
            if (hoverVisible) {
                const [x, y] = xy(hover);
                ctx.strokeStyle = '#fff';
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.arc(x, y, 5, 0, Math.PI * 2);
                ctx.stroke();
            }
            ctx.fillStyle = theme.muted;
            ctx.font = mapFont;
            ctx.fillText(t('北 ↑'), 14, h - 56);
            if (!m.path.length && !m.series.some(s => s.points.length)) ctx.fillText(t('此圈没有可绘制的坐标'), 20, h / 2);
            const desired = 90 / scale,
                base = Math.pow(10, Math.floor(Math.log10(desired)));
            const metres = base * ([5, 2, 1].find(k => k * base <= desired) || 1);
            ctx.strokeStyle = theme.muted;
            ctx.beginPath();
            ctx.moveTo(16, h - 24);
            ctx.lineTo(16 + metres * scale, h - 24);
            ctx.stroke();
            ctx.fillText(`${fmt(metres,metres<1?1:0)} m`, 16, h - 31);
        }
        canvas.addEventListener('keydown', e => {
            const step = 40 / scale;
            if (e.key.startsWith('Arrow') && ['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(e.key)) manualPan();
            if (e.key === 'ArrowLeft') cx -= step;
            else if (e.key === 'ArrowRight') cx += step;
            else if (e.key === 'ArrowUp') cy += step;
            else if (e.key === 'ArrowDown') cy -= step;
            else if (e.key === '+' || e.key === '=') scale *= 1.2;
            else if (e.key === '-') scale /= 1.2;
            else if (e.key === 'Home') {
                e.preventDefault();
                fit();
                return;
            } else return;
            e.preventDefault();
            draw();
        });
        canvas.addEventListener('wheel', e => {
            e.preventDefault();
            const rect = canvas.getBoundingClientRect(),
                x = e.clientX - rect.left - w / 2,
                y = e.clientY - rect.top - h / 2;
            const before = {
                x: cx + x / scale,
                y: cy - y / scale
            };
            scale = Math.max(.005, Math.min(1000, scale * Math.exp(-e.deltaY * .001)));
            cx = before.x - x / scale;
            cy = before.y + y / scale;
            draw();
        }, {
            passive: false
        });
        canvas.addEventListener('pointerdown', e => {
            drag = {
                x: e.clientX,
                y: e.clientY,
                cx,
                cy
            };
            canvas.setPointerCapture(e.pointerId);
            hideTip();
        });
        canvas.addEventListener('pointerup', () => {
            drag = null;
        });
        canvas.addEventListener('pointercancel', () => {
            drag = null;
        });
        canvas.addEventListener('dblclick', fit);
        canvas.addEventListener('pointerleave', () => {
            if (!drag) {
                hover = null;
                hideTip();
                draw();
            }
        });
        function manualPan() {
            cameraTarget = null;
            canvas.dispatchEvent(new Event('manualpan'));
        }
        canvas.addEventListener('pointermove', e => {
            if (drag) {
                if (e.clientX === drag.x && e.clientY === drag.y) return;
                manualPan();
                cx = drag.cx - (e.clientX - drag.x) / scale;
                cy = drag.cy + (e.clientY - drag.y) / scale;
                draw();
                return;
            }
            const m = getModel(),
                rect = canvas.getBoundingClientRect(),
                px = e.clientX - rect.left,
                py = e.clientY - rect.top;
            let best = null,
                dist = 24 * 24;
            for (const a of m.candidates || []) if (layers.line && layers['candidate-'+a.id]) {
                for (let i=0;i+1<a.points.length;i++) {
                    const p=a.points[i],q=a.points[i+1], [ax,ay]=xy(p),[bx,by]=xy(q),dx=bx-ax,dy=by-ay;
                    const t=Math.max(0,Math.min(1,((px-ax)*dx+(py-ay)*dy)/(dx*dx+dy*dy||1)));
                    const ds=(px-ax-t*dx)**2+(py-ay-t*dy)**2;
                    if (ds<dist) { dist=ds; best={x:p.x+t*(q.x-p.x),y:p.y+t*(q.y-p.y),s:p.s+t*(q.s-p.s),candidateId:a.id,actor:a.name}; }
                }
            }
            for (const s of m.series)
                for (let i = 0; i < s.points.length; i++) {
                    const p = s.points[i],
                        [x, y] = xy(p),
                        ds = (px - x) ** 2 + (py - y) ** 2;
                    if (ds < dist) {
                        dist = ds;
                        best = {
                            ...p,
                            actor: s.name,
                            actorId: s.id || s.key,
                            index: p.index ?? p.sample_index ?? i
                        };
                    }
                }
            if (!best) {
                const points = m.path;
                for (let i = 0; i + 1 < points.length; i++) {
                    const a = points[i],
                        b = points[i + 1],
                        [ax, ay] = xy(a),
                        [bx, by] = xy(b),
                        dx = bx - ax,
                        dy = by - ay,
                        t = Math.max(0, Math.min(1, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy || 1)));
                    const ds = (px - ax - t * dx) ** 2 + (py - ay - t * dy) ** 2;
                    if (ds < dist) {
                        dist = ds;
                        best = {
                            x: a.x + t * (b.x - a.x),
                            y: a.y + t * (b.y - a.y),
                            s: a.s + t * (b.s - a.s)
                        };
                    }
                }
            }
            hover = best;
            if (best) {
                let row = null,
                    rd = Infinity;
                for (const r of m.rows)
                    if (Number.isFinite(best.s) && Math.abs(r.s - best.s) < rd) {
                        rd = Math.abs(r.s - best.s);
                        row = r;
                    } let text = `${best.actor || '赛道路线'}\n${Number.isFinite(best.s) ? '本圈 '+fmt(best.s)+' m' : '里程未记录'}`;
                if (best.actor && !best.candidateId) text += `\n比赛时间 ${fmt(best.t, 2)} s · 速度 ${fmt(best.speed, 1)} km/h`;
                if (row) text += `\n可利用通道 ${spans(row.available ?? row.usable ?? row.walk)}\n障碍净距 ${fmt(m.params.obstacle_clearance)} m`;
                tip(text, e);
            } else hideTip();
            draw();
        });
        const resize = () => {
            if (destroyed) return;
            const rect = canvas.getBoundingClientRect(),
                dpr = devicePixelRatio || 1;
            if (rect.width < 2 || rect.height < 2) return;
            w = rect.width;
            h = rect.height;
            canvas.width = Math.round(w * dpr);
            canvas.height = Math.round(h * dpr);
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
            fit();
        };
        let destroyed = false;
        const refreshMapFont = () => { if (!destroyed) { mapFont = resolveMapFont(); staticCache = null; draw(); } };
        const unsubscribeLanguage = I18n.onChange(refreshMapFont);
        document.fonts?.ready.then(refreshMapFont);
        document.fonts?.addEventListener('loadingdone', refreshMapFont);
        const observer = new ResizeObserver(resize);
        observer.observe(canvas);
        return {
            draw,
            fit,
            refresh: resize,
            reset: () => { manualPan(); fit(); },
            center: point => { cameraTarget = point ? {x:point.x,y:point.y} : null; },
            highlight: (p, actorId) => {
                hover = p ? {...p, actorId} : null;
                draw();
            },
            destroy: () => { destroyed = true; unsubscribeLanguage(); observer.disconnect(); document.fonts?.removeEventListener('loadingdone', refreshMapFont); }
        };
    }
    $('#course').onchange = () => {
        coursePicker.close();
        state.course = $('#course').value;
        state.race = '';
        bindText($('#new-races'), '');
        const candidates = (state.catalog?.races || []).filter(r => r.course_id === state.course);
        state.raceFilter = candidates.some(r => r.status === 'finished') ? 'finished' : 'all';
        document.querySelectorAll('#race-filter [data-filter]').forEach(button => button.setAttribute('aria-pressed', String(button.dataset.filter === state.raceFilter)));
        const races = listedRaces(candidates, false);
        state.race = races[0]?.id || '';
        options($('#race'), races, state.race, raceLabel);
        controls();
        state.mode === 'plan' ? loadCourse() : loadRace();
    };
    document.querySelectorAll('#race-filter [data-filter]').forEach(button => button.onclick = () => {
        state.raceFilter = button.dataset.filter;
        const races = listedRaces((state.catalog?.races || []).filter(r => r.course_id === state.course), false);
        if (!races.some(r => r.id === state.race)) state.race = races[0]?.id || '';
        options($('#race'), races, state.race, raceLabel);
        controls(); loadRace();
    });
    $('#race').onchange = () => {
        state.race = $('#race').value;
        loadRace();
    };
    for (const mode of ['plan', 'record']) $(`#${mode}-mode`).onclick = () => {
        if (state.mode === mode) return;
        state.mode = mode;
        controls();
        mode === 'plan' ? loadCourse() : loadRace();
    };
    window.addEventListener('race-settings-saved', () => {
        if (state.mode === 'plan') loadCourse();
    });
    $('#calculate').onclick = () => loadCourse(true);
    $('#refresh').onclick = () => catalog();
    $('#reload').onclick = () => state.mode === 'plan' ? loadCourse() : loadRace();
    $('#maintenance-clear').onclick = clearRecordsAndCache;
    let copyStatusTimer;
    $('#record-id').onclick = async () => {
        const id = $('#record-id').textContent;
        clearTimeout(copyStatusTimer);
        try {
            await navigator.clipboard.writeText(id);
            if ($('#record-id').textContent !== id) return;
            bindText($('#record-copy-status'), '✓ 已复制');
        } catch {
            if ($('#record-id').textContent !== id) return;
            const selection = window.getSelection();
            const range = document.createRange();
            range.selectNodeContents($('#record-id'));
            selection.removeAllRanges(); selection.addRange(range);
            bindText($('#record-copy-status'), '请按 Ctrl+C');
        }
        copyStatusTimer = setTimeout(() => { bindText($('#record-copy-status'), ''); }, 1800);
    };
    const compactLayout = window.matchMedia('(max-width: 900px)');
    const workbench = $('.workbench');
    const sidebarToggle = $('#sidebar-toggle');
    let sidebarOpen = false;
    function syncSidebar() {
        const expanded = !compactLayout.matches || sidebarOpen;
        workbench.classList.toggle('sidebar-open', compactLayout.matches && sidebarOpen);
        sidebarToggle.setAttribute('aria-expanded', String(expanded));
        $('#sidebar').inert = !expanded;
        $('#sidebar').setAttribute('aria-hidden', String(!expanded));
        if (!expanded) coursePicker.close();
        bindText(sidebarToggle, compactLayout.matches && sidebarOpen ? '收起选项' : '赛事选项');
    }
    sidebarToggle.onclick = () => {
        sidebarOpen = !sidebarOpen;
        syncSidebar();
    };
    document.addEventListener('keydown', event => {
        if (event.key === 'Escape' && compactLayout.matches && sidebarOpen) {
            sidebarOpen = false;
            syncSidebar();
            sidebarToggle.focus();
        }
    });
    compactLayout.addEventListener('change', syncSidebar);
    function syncLanguage() {
        document.title = 'The Jockey Club · ' + t('赛马会会所');
        for (const [node, fields] of bindings) {
            if (!node.isConnected) { bindings.delete(node); continue; }
            for (const [property, source] of fields) {
                if (property.startsWith('@')) node.setAttribute(property.slice(1), t(typeof source === 'function' ? source() : source));
                else node[property] = t(typeof source === 'function' ? source() : source);
            }
        }
        for (const button of document.querySelectorAll('[data-language]')) button.setAttribute('aria-pressed', String(button.dataset.language === I18n.language));
        if (state.catalog) {
            // Replace option captions only; retain exact IDs, order and selected record.
            for (const option of $('#course').options) {
                const course = state.catalog.courses.find(item => item.id === option.value);
                if (course) bindText(option, courseOptionTitle(course));
            }
            for (const option of $('#race').options) {
                const race = state.catalog.races.find(item => item.id === option.value);
                if (race) bindText(option, raceLabel(race));
            }
            controls();
        }
        syncSidebar();
        hideTip();
    }
    for (const node of document.querySelectorAll('[data-i18n]')) bindText(node, node.dataset.i18n);
    for (const name of ['title', 'aria-label']) for (const node of document.querySelectorAll('[data-i18n-' + name + ']')) bindAttr(node, name, node.getAttribute('data-i18n-' + name));
    for (const button of document.querySelectorAll('[data-language]')) button.onclick = () => I18n.setLanguage(button.dataset.language);
    I18n.onChange(syncLanguage);
    syncLanguage();
    initializeSettings();
    catalog();
    setInterval(catalog, 3000);
})();

'use strict';

window.RaceUI = (() => {
    const dictionary = {
        "播放": "Play",
        "暂停": "Pause",
        "播放或暂停此圈": "Play or pause this lap",
        "回放速度": "Playback speed",
        "此圈回放时间": "Lap replay time",
        "跟随": "Follow",
        "回放跟随选手": "Follow rider",
        "跟随时选手始终居中；拖动地图或方向键切换自由视角；缩放保留跟随": "Keep rider centered; drag or use arrow keys for free view; zoom keeps following",
        "自由视角": "Free view",
        "玩家与主办人速度，以及玩家领先距离": "Player and host speeds, and player lead distance",
        "回放实时排名与选手状态": "Live replay standings and rider status",
        "实时排名": "Standings",
        "速度爆发": "Speed burst",
        "兴奋剂": "Stimulant",
        "出局": "Out",
        "无采样": "No sample",
        "爆发": "Burst",
        "此记录未保存药效／爆发状态": "Stimulant / burst state was not recorded",
        "缺少此圈时间区间": "Lap time interval unavailable",
        "速度 km/h · 比赛时间 s": "Speed km/h · Race time s",
        "速度 km/h · 玩家本圈进程 m": "Speed km/h · Player lap progress m",
        "速度图横轴": "Speed chart X axis",
        "距离": "Distance",
        "时间": "Time",
        "比较双方经过同一距离时的速度；领先距离取玩家经过此处的同刻值": "Compare speeds at the same course distance; lead is measured when the player reaches that distance",
        "比较同一比赛时刻双方各自的速度；圈次时间区间以玩家为准": "Compare both speeds at the same race time; the lap window follows the player",
        "此记录没有可用赛道距离": "Course distance is unavailable in this record",
        "玩家领先 m": "Player lead m",
        "领先距离未记录": "Lead distance not recorded",
        "未记录": "Not recorded",
        " · 兴奋剂": " · Stimulant",
        " · 速度爆发": " · Speed burst",
        "比赛时间": "Race time",
        "玩家本圈": "Player lap",
        "此刻速度": "Speed now",
        "各自经过此米速度": "Speed at this distance",
        "玩家": "Player",
        "主办人": "Host",
        "速度": "Speed",
        "{name}轨迹与位置": "{name} trail and position",
        "显示／隐藏 {name} 的轨迹与位置": "Show / hide {name} trail and position",
        "玩家同刻领先距离": "Player lead at the same time",
        "玩家经过此米时的领先距离": "Player lead when reaching this distance",
        "（正值领先）": " (positive means ahead)"
};
    window.RaceI18n?.register(dictionary);
    const t = (source, values = {}) => window.RaceI18n?.t(source, values) ?? source.replace(/\{(\w+)\}/g, (_, key) => values[key] ?? `{${key}}`);

    // Resolve the CSS tokens once; animation frames only read this immutable palette.
    const defaults = {bg:'#161616', panel:'#1c1c1c', map:'#121212', fg:'#e9e6df', muted:'#b3b0aa', quiet:'#96938e', edge:'#393837', player:'#f3f0e8', host:'#edc447', boost:'#e3483e', stimulant:'#edc447', gap:'#8eaaa9', survey:'#8d9697'};
    const css = typeof document === 'undefined' ? null : getComputedStyle(document.documentElement);
    const theme = Object.freeze(Object.fromEntries(Object.entries(defaults).map(([key,fallback]) => [key, css?.getPropertyValue(`--${key}`).trim() || fallback])));
    const colors = theme;
    const valid = value => value != null && Number.isFinite(Number(value));
    const format = (value, digits = 1) => valid(value) ? Number(value).toFixed(digits) : '—';
    const element = (tag, className, text) => {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (text != null) node.textContent = text;
        return node;
    };

    // Interpolate only within an explicit uninterrupted actor block.
    function position(points, time, includeOut = false) {
        let lo = 0,
            hi = points.length - 1;
        if (!points.length || time < points[0].t || time > points[hi].t) return null;
        while (lo < hi) {
            const mid = Math.ceil((lo + hi) / 2);
            if (points[mid].t <= time) lo = mid;
            else hi = mid - 1;
        }
        const a = points[lo];
        if (a.t === time) return a.out && !includeOut ? null : a;
        if (a.out) return includeOut ? a : null;
        const b = points[lo + 1];
        if (!b || b.out || a.chunk == null || a.chunk !== b.chunk || a.entity_id !== b.entity_id || b.t <= a.t || b.t - a.t > 0.3 + 1e-9) return null;
        const fraction = (time - a.t) / (b.t - a.t);
        const result = {
            ...a
        };
        for (const key of ['x', 'y', 'z', 's', 'speed_x', 'speed'])
            if (valid(a[key]) && valid(b[key])) result[key] = a[key] + fraction * (b[key] - a[key]);
        if (valid(a.heading) && valid(b.heading)) {
            const difference = b.heading-a.heading;
            result.heading = a.heading+Math.atan2(Math.sin(difference),Math.cos(difference))*fraction;
        }
        return result;
    }

    function riderState(role, point) {
        if (point?.out) return 'out';
        if (!point) return 'missing';
        if (role === 'player') return point.stimulant === true ? 'stimulant' : point.stimulant === false ? 'racing' : 'unknown';
        return point.boost === true ? 'boost' : point.boost === false || role === 'jockey' ? 'racing' : 'unknown';
    }

    function markerColor(role, point) {
        const state = riderState(role, point);
        return state === 'boost' ? colors.boost : state === 'stimulant' ? colors.stimulant : role === 'host' ? colors.host : colors.player;
    }

    function drawMarker(ctx, marker, x, y) {
        ctx.save(); ctx.translate(x, y);
        ctx.fillStyle = markerColor(marker.role, marker);
        ctx.strokeStyle = '#0a0a0a'; ctx.lineWidth = 2;
        ctx.beginPath();
        if (marker.role === 'player' && valid(marker.heading)) {
            // World +Y is north, canvas +Y is down; the native forward vector
            // remains authoritative when the horse is standing still.
            ctx.rotate(Math.PI/2-marker.heading);
            ctx.moveTo(0,-9); ctx.bezierCurveTo(4,-4,7,0,6,3);
            ctx.bezierCurveTo(4,9,-4,9,-6,3); ctx.bezierCurveTo(-7,0,-4,-4,0,-9);
        } else ctx.arc(0,0,5.5,0,Math.PI*2);
        ctx.closePath(); ctx.fill(); ctx.stroke(); ctx.restore();
    }

    function standings(actors, time) {
        const rows = actors.map((actor, index) => {
            const point = position(actor.points, time, true);
            const state = riderState(actor.role, point);
            return {actor, point, state, index, rank:null};
        });
        const ordered = rows.filter(r => r.point && !r.point.out && valid(r.point.s))
            .sort((a,b) => b.point.s-a.point.s || a.index-b.index);
        ordered.forEach((row,index) => { row.rank = index && Math.abs(ordered[index-1].point.s-row.point.s)<.01 ? ordered[index-1].rank : index+1; });
        return [...ordered, ...rows.filter(r => r.rank === null)];
    }

    // A chart value exists only where a valid adjacent segment crosses the query.
    function interpolateAt(points, target, axis, field, ranges = [[0, points.length]]) {
        if (!valid(target)) return null;
        for (const [first, end] of ranges) for (let index = first; index < end; index++) {
            const a = points[index];
            const ax = axis(a);
            if (valid(ax) && ax === target && valid(a[field]) && !a.out) return {
                ...a
            };
            const b = points[index + 1];
            if (!b || !valid(ax) || !valid(axis(b)) || !valid(a[field]) || !valid(b[field]) || a.out || b.out) continue;
            if (a.chunk == null || a.chunk !== b.chunk || a.entity_id !== b.entity_id) continue;
            if (!valid(a.t) || !valid(b.t) || b.t <= a.t || b.t - a.t > .3 + 1e-9) continue;
            const bx = axis(b);
            // An exact endpoint owns its recorded discrete state (dose/burst),
            // rather than inheriting the preceding segment's state.
            if (ax === bx || target === bx || target < Math.min(ax, bx) || target > Math.max(ax, bx)) continue;
            const fraction = (target - ax) / (bx - ax);
            const result = {
                ...a
            };
            for (const key of ['t', 's', 'speed_x', 'x', 'y', 'z', 'speed', 'gap']) {
                if (valid(a[key]) && valid(b[key])) result[key] = a[key] + fraction * (b[key] - a[key]);
            }
            return result;
        }
        return null;
    }

    // Index source positions, never resample or sort the recorded trajectory.
    // Each leaf bounds 128 consecutive segments, including its far endpoint.
    // This also works for reversals and discontinuous/non-monotone progress.
    function createSampleIndex(points, axis) {
        const blockSize = 128, values = new Float64Array(points.length), ordered = [];
        let monotone = true, previous = -Infinity;
        for (let i = 0; i < points.length; i++) {
            const value = axis(points[i]);
            values[i] = valid(value) ? Number(value) : NaN;
            if (!Number.isFinite(values[i])) continue;
            if (values[i] < previous) monotone = false;
            previous = values[i]; ordered.push(values[i]);
        }
        if (!monotone) ordered.sort((a,b) => a-b);
        const blocks = Math.ceil(points.length/blockSize);
        let leaves = 1; while (leaves < blocks) leaves *= 2;
        const low = new Float64Array(leaves*2).fill(Infinity), high = new Float64Array(leaves*2).fill(-Infinity);
        for (let block = 0; block < blocks; block++) {
            for (let i = block*blockSize; i <= Math.min(points.length-1,(block+1)*blockSize); i++) {
                if (!Number.isFinite(values[i])) continue;
                low[leaves+block] = Math.min(low[leaves+block],values[i]);
                high[leaves+block] = Math.max(high[leaves+block],values[i]);
            }
        }
        for (let i = leaves-1; i; i--) {
            low[i] = Math.min(low[i*2],low[i*2+1]); high[i] = Math.max(high[i*2],high[i*2+1]);
        }
        function ranges(minimum, maximum) {
            const result = [];
            function visit(node, first, count) {
                if (high[node] < minimum || low[node] > maximum || low[node] === Infinity) return;
                if (node >= leaves) {
                    const start = first*blockSize, end = Math.min(points.length,start+blockSize);
                    if (result.length && result.at(-1)[1] === start) result.at(-1)[1] = end;
                    else result.push([start,end]);
                } else {
                    visit(node*2,first,count/2); visit(node*2+1,first+count/2,count/2);
                }
            }
            visit(1,0,leaves); return result;
        }
        return {
            values, ranges,
            get minimum() { return ordered[0]; }, get maximum() { return ordered.at(-1); },
            at(target, field) { return interpolateAt(points,target,axis,field,ranges(target,target)); },
            window(target, count, minimum=-Infinity, maximum=Infinity) {
                if (!ordered.length) return null;
                const bound=(value,strict=false)=>{
                    let lo=0,hi=ordered.length;
                    while(lo<hi){const mid=(lo+hi)>>>1;if(ordered[mid]<value||(strict&&ordered[mid]===value))lo=mid+1;else hi=mid;}
                    return lo;
                };
                const begin=bound(minimum),end=bound(maximum,true);
                if(begin===end)return null;
                count = Math.min(end-begin,Math.max(2,count));
                const first = Math.max(begin,Math.min(end-count,bound(target)-Math.floor(count/2)));
                return [ordered[first],ordered[first+count-1]];
            }
        };
    }

    // View-only X navigation. Recorded points and speed/gap interpolation stay untouched.
    function createXViewport(minimum, maximum) {
        const low = Number.isFinite(minimum) ? minimum : 0;
        const high = Number.isFinite(maximum) ? Math.max(low,maximum) : low+1;
        const full = high-low, minimumSpan = full/1000;
        let start = low, span = full;
        const moveTo = value => { start = Math.max(low,Math.min(high-span,value)); };
        return {
            get start() { return start; }, get end() { return start+span; }, get span() { return span; },
            get minimum() { return low; }, get maximum() { return high; },
            reset() { start=low;span=full; },
            setRange(first, last) {
                if (!Number.isFinite(first) || !Number.isFinite(last) || last <= first) return;
                span=Math.min(full,last-first); moveTo(first);
            },
            moveTo(value) { if(Number.isFinite(value))moveTo(value); },
            pan(delta) { if(Number.isFinite(delta))moveTo(start+delta); },
            zoom(factor, anchor=start+span/2) {
                if(!full||!Number.isFinite(factor)||factor<=0||!Number.isFinite(anchor))return;
                const fraction=Math.max(0,Math.min(1,(anchor-start)/span));
                span=Math.max(Math.min(minimumSpan,span),Math.min(full,span*factor));
                moveTo(anchor-fraction*span);
            }
        };
    }

    let activeReplay = null;

    function replay(card, lap, series, model, viewer, options = {}) {
        const replayData = lap.replay || {};
        const actors = (replayData.actors || series.map(s => ({
                id: s.key,
                role: s.key,
                name: s.name,
                points: s.points
            })))
            .map(actor => ({
                ...actor,
                points: (actor.points || []).filter(p => valid(p.t) && valid(p.x) && valid(p.y))
            }));
        const player = actors.find(a => a.role === 'player');
        const start = Number(replayData.start_t ?? player?.points[0]?.t);
        const end = Number(replayData.end_t ?? player?.points.at(-1)?.t);
        const trajectories = actors.map(actor => ({
            id: actor.id,
            key: actor.role,
            name: actor.name,
            points: actor.points.filter(p => p.t >= start && p.t <= end && !p.out)
        }));
        const visibleTracks = new Set(actors.map(actor => actor.id));
        model.series = trajectories;
        const available = Number.isFinite(start) && Number.isFinite(end) && end > start;
        let time = available ? start : 0,
            playing = false,
            frame = 0,
            last = 0,
            visible = options.managed ? false : true;
        const controls = element('div', 'replay-controls');
        const play = element('button', 'replay-play'); play.type='button';
        play.disabled = !available;
        function syncPlay() {
            const label=t(playing?'暂停':'播放');
            play.title=label;play.setAttribute('aria-label',label);play.setAttribute('aria-pressed',String(playing));
            play.innerHTML=playing?'<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M7 5h4v14H7zm6 0h4v14h-4Z"/></svg>':'<svg viewBox="0 0 24 24" aria-hidden="true"><path d="m8 4 12 8-12 8Z"/></svg>';
        }
        syncPlay();
        const sequence=element('button','replay-sequence-toggle'); sequence.type='button'; sequence.hidden=typeof options.setContinuous!=='function';
        const syncSequence=()=>{const continuous=options.getContinuous?.()??false;sequence.textContent=t(continuous?'连播':'本圈');sequence.title=t(continuous?'连续播放':'仅当前圈');sequence.setAttribute('aria-label',sequence.title);sequence.setAttribute('aria-pressed',String(continuous));};
        sequence.onclick=()=>{options.setContinuous?.(!(options.getContinuous?.()??false));syncSequence();};
        syncSequence();
        const rate = element('select');
        rate.setAttribute('aria-label', t('回放速度'));
        [1, 2, 3, 4, 8].forEach(value => rate.append(new Option(`${value}×`, value)));
        const range = element('input', 'replay-range');
        range.dataset.testid = 'replay-range';
        range.type = 'range';
        range.min = available ? start : 0;
        range.max = available ? end : 1;
        range.step = '.001';
        range.disabled = !available;
        range.setAttribute('aria-label', t('此圈回放时间'));
        const clock = element('output', 'replay-clock');
        const followLabel = element('label', 'replay-follow-label', t('跟随'));
        const follow = element('select', 'replay-follow');
        follow.setAttribute('aria-label', t('回放跟随选手'));
        follow.title = t('跟随时选手始终居中；拖动地图或方向键切换自由视角；缩放保留跟随');
        follow.append(new Option(t('自由视角'), ''));
        actors.forEach(actor => follow.append(new Option(t(actor.name), actor.id)));
        follow.value = player?.id ?? '';
        followLabel.append(follow);
        follow.onchange = () => update();
        const manualPan = () => { follow.value = ''; };
        card.canvas.addEventListener('manualpan', manualPan);
        controls.append(play, sequence, rate, followLabel, range, clock);
        (card.map || card.e).append(controls);
        const speed = element('canvas', 'speed');
        speed.setAttribute('role', 'img'); speed.tabIndex=0;
        speed.setAttribute('aria-label', t('速度对比图。滚轮缩放，拖动平移，方向键平移，加减键缩放，Home复位。'));
        const chartView=element('div','speed-chart');
        const chartReset=element('button','chart-reset'); chartReset.type='button';
        chartReset.innerHTML='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 10a8 8 0 1 1 1 8M4 4v6h6" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>';
        const syncChartReset=()=>{chartReset.title=t('复原速度图');chartReset.setAttribute('aria-label',chartReset.title);};syncChartReset();
        const chartTools=element('div','speed-chart-tools');
        const axisGroup=element('div','chart-axis'); axisGroup.setAttribute('role','group');
        const canDistance=lap.speed_axis!=='time';
        let chartAxis=canDistance?'distance':'time';
        const axisButtons=new Map(['distance','time'].map(axis=>{
            const button=element('button','chart-axis-button'); button.type='button'; button.dataset.axis=axis;
            button.disabled=axis==='distance'&&!canDistance;
            button.onclick=()=>selectChartAxis(axis);
            axisGroup.append(button);
            return [axis,button];
        }));
        function syncChartAxis() {
            axisGroup.setAttribute('aria-label',t('速度图横轴'));
            for(const [axis,button] of axisButtons) {
                button.textContent=t(axis==='time'?'时间':'距离');
                button.setAttribute('aria-pressed',String(chartAxis===axis));
                button.title=t(button.disabled?'此记录没有可用赛道距离':axis==='time'?'比较同一比赛时刻双方各自的速度；圈次时间区间以玩家为准':'比较双方经过同一距离时的速度；领先距离取玩家经过此处的同刻值');
            }
        }
        syncChartAxis();
        chartTools.append(axisGroup,chartReset); chartView.append(chartTools,speed);
        (card.chart || card.e).insertBefore(chartView,card.note);
        // Replay actors share the player's lap clock and retain the adjacent
        // boundary samples, even when the host is on a different course lap.
        const timeSeries=series.map(track=>({...track,points:actors.find(actor=>actor.role===track.key)?.points||[]}));
        const graph = speedChart(speed, series, lap.gap_series || [], viewer, chartAxis, {timeSeries,start,end,distanceEnd:lap.length});
        function selectChartAxis(axis) {
            if((axis!=='distance'&&axis!=='time')||(axis==='distance'&&!canDistance))return;
            chartAxis=axis; graph.setAxis(axis); syncChartAxis();
        }
        chartReset.onclick=()=>graph.reset();
        const panel = element('section', 'standings-panel');
        panel.setAttribute('aria-label',t('回放实时排名与选手状态'));
        const panelHead = element('button','standings-heading',t('实时排名'));
        panelHead.setAttribute('aria-expanded','true');
        const table = element('div','standings-body');
        const body = element('div','standings-rows');
        const legend = element('div','state-legend');
        for (const [kind,label] of [['boost',t('速度爆发')],['stimulant',t('兴奋剂')]]) {
            const item = element('span','state-key');
            const mark = element('i','rider-symbol');
            mark.dataset.role = kind === 'stimulant' ? 'player' : 'host';
            mark.dataset.state = kind;
            item.append(mark,document.createTextNode(label)); legend.append(item);
        }
        table.append(body,legend); panel.append(panelHead,table); card.stage.append(panel);
        panelHead.onclick = () => { table.hidden = !table.hidden; panelHead.setAttribute('aria-expanded',String(!table.hidden)); };
        const rows = new Map(actors.map(actor => {
            const row = element('div','standing-row');
            row.dataset.actor = actor.id;
            const rank = element('span','standing-rank');
            const symbol = element('i','rider-symbol'); symbol.dataset.role = actor.role;
            const name = element('span','standing-name',t(actor.name));
            const status = element('span','standing-status');
            const track = element('button','rider-track-toggle');
            track.setAttribute('role','switch');
            track.setAttribute('aria-checked','true');
            track.setAttribute('aria-label',t('{name}轨迹与位置', {name:t(actor.name)}));
            track.title = t('显示／隐藏 {name} 的轨迹与位置', {name:t(actor.name)});
            track.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12Z"/><circle cx="12" cy="12" r="3"/><path class="eye-slash" d="m3 3 18 18"/></svg>';
            track.onclick = () => {
                visibleTracks.has(actor.id) ? visibleTracks.delete(actor.id) : visibleTracks.add(actor.id);
                track.setAttribute('aria-checked',String(visibleTracks.has(actor.id)));
                row.dataset.visible = String(visibleTracks.has(actor.id));
                model.series = trajectories.filter(t => visibleTracks.has(t.id));
                update();
            };
            row.append(rank,symbol,name,status,track); body.append(row);
            return [actor.id,{row,rank,symbol,status}];
        }));
        let lastBoardTime = null;
        function updateBoard(force = false) {
            if (!force && lastBoardTime !== null && Math.abs(time-lastBoardTime)<.1 && playing && time < end) return;
            lastBoardTime = time;
            const order = standings(actors,time);
            order.forEach((entry,index) => {
                const item = rows.get(entry.actor.id);
                item.row.style.order = index;
                item.rank.textContent = entry.rank ?? '—';
                item.symbol.dataset.state = entry.state;
                item.row.dataset.state = entry.state;
                item.status.textContent = ({out:t('出局'),missing:t('无采样'),boost:t('爆发'),stimulant:t('兴奋剂'),unknown:'—',racing:''})[entry.state];
                item.row.title = entry.state === 'unknown' ? t('此记录未保存药效／爆发状态') : valid(entry.point?.speed) ? `${format(entry.point.speed)} km/h` : '';
            });
        }

        function update() {
            range.value = time;
            clock.textContent = available ? `${format(time-start)} / ${format(end-start)} s` : t('缺少此圈时间区间');
            model.markers = actors.filter(actor => visibleTracks.has(actor.id)).flatMap(actor => {
                const point = position(actor.points, time);
                return point ? [{
                    ...point,
                    role: actor.role,
                    name: actor.name
                }] : [];
            });
            card.canvas.dataset.replayTime = time;
            card.canvas.dataset.markerCount = model.markers.length;
            card.canvas.dataset.markers = JSON.stringify(model.markers.map(marker => ({
                role: marker.role,
                name: marker.name,
                state: riderState(marker.role, marker),
                color: markerColor(marker.role, marker),
                heading: marker.heading ?? null,
                x: marker.x,
                y: marker.y
            })));
            const followed = actors.find(actor => actor.id === follow.value);
            // Position uses the same lap clock and interpolation as every marker.
            // A hidden rider may still be followed without restoring its visibility.
            const target = followed ? position(followed.points, time) : null;
            viewer.center(target);
            card.canvas.dataset.followActor = follow.value;
            card.canvas.dataset.followAvailable = String(Boolean(target));
            viewer.draw();
            graph.setTime(time);
            updateBoard();
        }

        function pause() {
            playing = false;
            syncPlay();
            cancelAnimationFrame(frame);
            last = 0;
            updateBoard(true);
        }

        function tick(now) {
            if (!playing || document.hidden || !visible) {
                pause();
                return;
            }
            if (last) time = Math.min(end, time + (now - last) / 1000 * Number(rate.value));
            last = now;
            update();
            if (time >= end) { const snapshot = capture(); pause(); options.onEnd?.(snapshot); }
            else frame = requestAnimationFrame(tick);
        }
        function startPlaying() {
            if (!available || document.hidden || !visible) return;
            activeReplay?.pause();
            activeReplay = {
                pause
            };
            if (time >= end) time = start;
            playing = true;
            syncPlay();
            frame = requestAnimationFrame(tick);
        }
        play.onclick = () => playing ? pause() : startPlaying();
        range.oninput = () => {
            time = Number(range.value);
            last = 0;
            update();
        };
        const visibility = () => {
            if (document.hidden) pause();
        };
        document.addEventListener('visibilitychange', visibility);
        // Managed lap pages own visibility; an old observer callback must not
        // pause a new lap after a continuous-play transition.
        const observer = options.managed ? null : new IntersectionObserver(entries => {
            visible = entries[0].isIntersecting;
            if (!visible) pause();
        });
        observer?.observe(card.e);
        function capture() {
            return {time, playing, rate:Number(rate.value), follow:follow.value, speedAxis:chartAxis,
                hidden:actors.filter(actor => !visibleTracks.has(actor.id)).map(actor => actor.id)};
        }
        function restore(snapshot = {}, {atStart = false, resume = snapshot.playing} = {}) {
            pause();
            if(snapshot.speedAxis)selectChartAxis(snapshot.speedAxis);
            if (snapshot.rate != null) rate.value = String(snapshot.rate);
            if (snapshot.follow != null) follow.value = actors.some(actor => actor.id === snapshot.follow) ? snapshot.follow : '';
            if (snapshot.hidden) {
                const hidden = new Set(snapshot.hidden);
                for (const actor of actors) {
                    hidden.has(actor.id) ? visibleTracks.delete(actor.id) : visibleTracks.add(actor.id);
                    const row = rows.get(actor.id).row;
                    row.dataset.visible = String(!hidden.has(actor.id));
                    row.querySelector('.rider-track-toggle').setAttribute('aria-checked',String(!hidden.has(actor.id)));
                }
                model.series = trajectories.filter(actor => visibleTracks.has(actor.id));
            }
            time = atStart ? start : valid(snapshot.time) ? Math.max(start, Math.min(end,Number(snapshot.time))) : time;
            update();
            if (resume) startPlaying();
        }
        const refreshLanguage = () => {
            syncPlay(); syncSequence(); syncChartReset(); syncChartAxis();
            for (const [node, source] of [[rate,'回放速度'],[range,'此圈回放时间'],[follow,'回放跟随选手'],[speed,'速度对比图。滚轮缩放，拖动平移，方向键平移，加减键缩放，Home复位。'],[panel,'回放实时排名与选手状态']]) node.setAttribute('aria-label', t(source));
            follow.title = t('跟随时选手始终居中；拖动地图或方向键切换自由视角；缩放保留跟随');
            followLabel.firstChild.textContent = t('跟随');
            follow.options[0].textContent = t('自由视角');
            actors.forEach((actor, index) => {
                follow.options[index+1].textContent = t(actor.name);
                const row = rows.get(actor.id).row;
                row.querySelector('.standing-name').textContent = t(actor.name);
                const track = row.querySelector('.rider-track-toggle');
                track.setAttribute('aria-label',t('{name}轨迹与位置',{name:t(actor.name)}));
                track.title = t('显示／隐藏 {name} 的轨迹与位置',{name:t(actor.name)});
            });
            panelHead.textContent = t('实时排名');
            [...legend.children].forEach((item,index) => item.lastChild.textContent = t(index ? '兴奋剂' : '速度爆发'));
            graph.refreshLanguage();
            updateBoard(true);
            update();
        };
        const unsubscribeLanguage = window.RaceI18n?.onChange(refreshLanguage);
        update();
        return {
            draw: update,
            capture, restore, pause,
            setActive(active) { visible = active; syncSequence(); if (!active) pause(); },
            get available() { return available; },
            destroy() {
                unsubscribeLanguage?.();
                pause();
                if (activeReplay?.pause === pause) activeReplay = null;
                observer?.disconnect();
                card.canvas.removeEventListener('manualpan', manualPan);
                graph.destroy();
                document.removeEventListener('visibilitychange', visibility);
            }
        };
    }

    function speedChart(canvas, series, gap, map, axis, options = {}) {
        const ctx = canvas.getContext('2d');
        const resolveFont = () => `12px ${getComputedStyle(canvas).getPropertyValue('--sans').trim() || 'system-ui'}`;
        let chartFont = resolveFont(), destroyed = false;
        const distanceSeries=series, timeSeries=options.timeSeries||series, views=new Map();
        const windowPoints = Math.max(2,Math.floor(options.windowPoints || 400));
        let timeAxis, viewport, maxV, view;
        const hasGap = gap.some(point => valid(point.gap));
        let width = 1,
            height = 1,
            currentTime = null,
            hover = null;
        const sx = p => { const value = timeAxis ? p.t : p.speed_x ?? p.s; return valid(value) ? Number(value) : NaN; };
        const maxGap = gap.reduce((maximum, point) => valid(point.gap) ? Math.max(maximum, Math.abs(point.gap)) : maximum, 10);
        const gapClock = createSampleIndex(gap,p=>p.t);
        function configureAxis(value) {
            timeAxis=value==='time';
            series=timeAxis?timeSeries:distanceSeries;
            if(!views.has(value)) {
                // Capture the axis: a saved index must not depend on the next mode.
                const coordinate=timeAxis?p=>valid(p.t)?Number(p.t):NaN:p=>valid(p.speed_x??p.s)?Number(p.speed_x??p.s):NaN;
                const indexes=series.map(track=>createSampleIndex(track.points,coordinate));
                let minimum=Infinity, maximum=-Infinity, speed=10;
                for (const track of series) for (const point of track.points) {
                    const at=coordinate(point);
                    if (!valid(at)||point.out) continue;
                    minimum=Math.min(minimum,at);maximum=Math.max(maximum,at);
                    if(valid(point.speed))speed=Math.max(speed,point.speed);
                }
                minimum=Math.max(timeAxis&&valid(options.start)?Number(options.start):0,minimum);
                if(timeAxis&&valid(options.end))maximum=Math.min(maximum,Number(options.end));
                if(!timeAxis&&valid(options.distanceEnd))maximum=Math.min(maximum,Number(options.distanceEnd));
                const playerIndex=Math.max(0,series.findIndex(track=>track.key==='player'));
                views.set(value,{viewport:createXViewport(minimum,maximum),indexes,playerIndex,
                    gapIndex:timeAxis?gapClock:createSampleIndex(gap,coordinate),
                    reference:indexes[playerIndex],automatic:true,maxV:Math.ceil(speed/10)*10});
            }
            view=views.get(value);viewport=view.viewport;maxV=view.maxV;
            canvas.dataset.speedAxis=timeAxis?'time':'distance';
            followWindow();
        }
        function cursorPoint() { return !valid(currentTime) ? null : timeAxis ? {t:currentTime} : position(series[view.playerIndex].points,currentTime); }
        function followWindow() {
            if(!view.automatic)return false;
            const cursor=cursorPoint(), target=cursor&&valid(sx(cursor))?sx(cursor):viewport.minimum;
            const selected=view.reference.window(target,windowPoints,viewport.minimum,viewport.maximum);
            if(!selected||selected[1]<=selected[0])return false;
            const oldStart=viewport.start,oldSpan=viewport.span;
            // Keep a fixed number of original samples around the player. Uneven
            // distance/time spacing may put the cursor slightly off the midpoint.
            viewport.setRange(selected[0],selected[1]);
            return viewport.start!==oldStart||viewport.span!==oldSpan;
        }
        configureAxis(axis==='time'?'time':'distance');
        let drag=null;
        const plotWidth=()=>Math.max(1,width-110);
        const x = s => 48 + (viewport.span?(s-viewport.start)/viewport.span:.5)*plotWidth();
        const atX = pixel => viewport.start + (pixel-48)/plotWidth()*viewport.span;
        // Keep adjacent time ticks distinct without exposing floating-point tails.
        const xDigits = () => timeAxis&&viewport.span>0 ? Math.max(0,Math.ceil(-Math.log10(viewport.span/4))) : 0;
        const y = v => height - 35 - v / maxV * (height - 70);
        const gy = v => 35 + (1 - v / maxGap) / 2 * (height - 70);

        function line(points, index, color, ordinate, field, dash = [], role = null) {
            ctx.strokeStyle = color;
            ctx.lineWidth = 1.6;
            ctx.setLineDash(dash);
            ctx.beginPath();
            for (const [first,end] of index.ranges(viewport.start,viewport.end)) {
              let previous = null;
              for (let i=first;i<Math.min(points.length,end+1);i++) {
                const point=points[i], coordinate=index.values[i];
                if (!Number.isFinite(coordinate) || !valid(point[field]) || point.out) {
                    previous = null;
                    continue;
                }
                if (!previous || point.chunk == null || point.chunk !== previous.chunk || point.entity_id !== previous.entity_id || (valid(point.t) && valid(previous.t) && (point.t <= previous.t || point.t - previous.t > 0.3 + 1e-9))) ctx.moveTo(x(coordinate), ordinate(point[field]));
                else {
                    const segmentColor = role ? markerColor(role, previous) : color;
                    if (ctx.strokeStyle !== segmentColor) {
                        ctx.stroke(); ctx.strokeStyle = segmentColor; ctx.beginPath();
                        ctx.moveTo(x(sx(previous)), ordinate(previous[field]));
                    }
                    ctx.lineTo(x(coordinate), ordinate(point[field]));
                }
                previous = point;
              }
            }
            ctx.stroke();
            ctx.setLineDash([]);
        }

        function draw() {
            ctx.save();
            ctx.setTransform(1, 0, 0, 1, 0, 0);
            ctx.clearRect(0, 0, canvas.width, canvas.height);
            ctx.restore();
            ctx.font = chartFont;
            ctx.fillStyle = theme.muted;
            ctx.fillText(timeAxis ? t('速度 km/h · 比赛时间 s') : t('速度 km/h · 玩家本圈进程 m'), 12, 18);
            ctx.textAlign='right';ctx.fillText(t('玩家领先 m'), width - 52, 18);ctx.textAlign='left';
            for (let i = 0; i <= 4; i++) {
                const yy = y(maxV * i / 4);
                ctx.strokeStyle = theme.edge;
                ctx.beginPath();
                ctx.moveTo(48, yy);
                ctx.lineTo(width - 62, yy);
                ctx.stroke();
                ctx.fillStyle = theme.muted;
                ctx.fillText(format(maxV * i / 4, 0), 12, yy + 4);
                if(viewport.span||i===2)ctx.fillText(format(viewport.start + viewport.span * i / 4, xDigits()), x(viewport.start + viewport.span * i / 4) - 10, height - 14);
                ctx.fillStyle = colors.gap;
                ctx.fillText(format(-maxGap + 2 * maxGap * i / 4, 0), width - 53, gy(-maxGap + 2 * maxGap * i / 4) + 4);
            }
            ctx.save();ctx.beginPath();ctx.rect(48,28,Math.max(1,width-110),Math.max(1,height-63));ctx.clip();
            series.forEach((s,i) => line(s.points, view.indexes[i], colors[s.key], y, 'speed', s.key === 'host' ? [5, 3] : [], s.key));
            line(gap, view.gapIndex, colors.gap, gy, 'gap', [2, 3]);
            ctx.restore();
            if (!hasGap) {
                ctx.fillStyle = theme.muted;
                ctx.fillText(t('领先距离未记录'), Math.max(50, width - 180), height - 52);
            }
        }
        let background = null, backgroundDirty = true;

        const drawable = () => !destroyed && width >= 2 && height >= 2 &&
            canvas.width > 0 && canvas.height > 0 && !canvas.closest?.('.lap-page')?.hidden;
        function render() {
            if (!drawable()) return;
            canvas.dataset.speedWindow=JSON.stringify({start:viewport.start,end:viewport.end,minimum:viewport.minimum,maximum:viewport.maximum,automatic:view.automatic,points:windowPoints});
            if (!background || backgroundDirty) {
                draw();
                if (!background) background = document.createElement('canvas');
                if (background.width !== canvas.width) background.width = canvas.width;
                if (background.height !== canvas.height) background.height = canvas.height;
                const g=background.getContext('2d');
                g.clearRect(0,0,background.width,background.height);g.drawImage(canvas,0,0);
                backgroundDirty = false;
            } else {
                ctx.save();
                ctx.setTransform(1, 0, 0, 1, 0, 0);
                ctx.clearRect(0, 0, canvas.width, canvas.height);
                ctx.drawImage(background, 0, 0);
                ctx.restore();
            }
            const cursor = hover || cursorPoint();
            if (cursor && valid(sx(cursor)) && sx(cursor)>=viewport.start && sx(cursor)<=viewport.end) {
                ctx.strokeStyle = theme.fg + '80';
                ctx.beginPath();
                ctx.moveTo(x(sx(cursor)), 28);
                ctx.lineTo(x(sx(cursor)), height - 35);
                ctx.stroke();
            }
        }
        const clearHover=()=>{if(hover)map.highlight(null);hover=null;const tip=document.querySelector('#tooltip');if(tip)tip.hidden=true;};
        const changed=()=>{backgroundDirty=true;clearHover();render();};
        canvas.addEventListener('wheel',event=>{
            event.preventDefault();
            view.automatic=false;
            const pixel=event.clientX-canvas.getBoundingClientRect().left;
            viewport.zoom(Math.exp(Math.max(-1000,Math.min(1000,event.deltaY))*.001),atX(Math.max(48,Math.min(width-62,pixel))));
            changed();
        },{passive:false});
        canvas.addEventListener('keydown',event=>{
            if(event.key==='Home'){view.automatic=true;followWindow();}
            else if(event.key==='+'||event.key==='='){view.automatic=false;viewport.zoom(.8);}
            else if(event.key==='-'){view.automatic=false;viewport.zoom(1.25);}
            else if(event.key==='ArrowLeft'){view.automatic=false;viewport.pan(-viewport.span*.1);}
            else if(event.key==='ArrowRight'){view.automatic=false;viewport.pan(viewport.span*.1);}
            else return;
            event.preventDefault();changed();
        });
        canvas.addEventListener('pointerdown',event=>{
            if(event.button!==0)return;
            drag={x:event.clientX,start:viewport.start,span:viewport.span};
            canvas.setPointerCapture(event.pointerId);canvas.dataset.dragging='true';clearHover();
        });
        const stopDrag=()=>{drag=null;canvas.dataset.dragging='false';};
        canvas.addEventListener('pointerup',stopDrag);canvas.addEventListener('pointercancel',stopDrag);
        canvas.addEventListener('pointermove', event => {
            if(drag){if(event.clientX===drag.x)return;view.automatic=false;viewport.moveTo(drag.start-(event.clientX-drag.x)/plotWidth()*drag.span);changed();return;}
            const rect = canvas.getBoundingClientRect(),
                mouseX = event.clientX - rect.left;
            const target = atX(mouseX);
            if(mouseX<48||mouseX>width-62){clearHover();render();return;}
            const playerPoint = view.indexes[0].at(target, 'speed');
            const hostPoint = view.indexes[1].at(target, 'speed');
            const gapTime=timeAxis?target:playerPoint?.t;
            const gapPoint = valid(gapTime) ? gapClock.at(gapTime, 'gap') : null;
            hover = playerPoint || hostPoint || (timeAxis?{t:target}:{speed_x:target});
            map.highlight(playerPoint || hostPoint, playerPoint ? 'player' : 'host');
            const tip = document.querySelector('#tooltip');
            if (hover) {
                const gapValue = valid(gapPoint?.gap) ? `${format(gapPoint.gap)} m` : t('未记录');
                const playerState = playerPoint?.stimulant === true ? t(' · 兴奋剂') : '';
                const hostState = hostPoint?.boost === true ? t(' · 速度爆发') : '';
                tip.textContent = `${timeAxis ? t('比赛时间') : t('玩家本圈')} ${format(sx(hover),timeAxis?Math.max(1,xDigits()):1)} ${timeAxis ? 's' : 'm'}\n${timeAxis ? t('此刻速度') : t('各自经过此米速度')}: ${t('玩家')} ${format(playerPoint?.speed)}${playerState} / ${t(series[1].name)} ${format(hostPoint?.speed)}${hostState} km/h\n${t(timeAxis?'玩家同刻领先距离':'玩家经过此米时的领先距离')}: ${gapValue}${t('（正值领先）')}`;
                tip.hidden = false;
                tip.style.left = `${Math.max(8, Math.min(innerWidth-260,event.clientX+12))}px`;
                tip.style.top = `${Math.max(8, Math.min(innerHeight-100,event.clientY+12))}px`;
            }
            render();
        });
        canvas.addEventListener('pointerleave', () => {
            hover = null;
            map.highlight(null);
            document.querySelector('#tooltip').hidden = true;
            render();
        });
        const resize = new ResizeObserver(() => {
            if (destroyed) return;
            const rect = canvas.getBoundingClientRect(),
                dpr = devicePixelRatio || 1;
            if(rect.width<2||rect.height<2)return;
            width = rect.width;
            height = rect.height;
            canvas.width = Math.round(width * dpr);
            canvas.height = Math.round(height * dpr);
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
            backgroundDirty = true;
            render();
        });
        resize.observe(canvas);
        const fontsReady = () => { if (!destroyed) { chartFont = resolveFont(); backgroundDirty = true; render(); } };
        document.fonts?.ready.then(fontsReady);
        document.fonts?.addEventListener('loadingdone', fontsReady);
        return {
            setAxis(value) {
                if(destroyed||(value!=='distance'&&value!=='time')||(value==='time')===timeAxis)return;
                configureAxis(value);stopDrag();changed();
            },
            reset() { view.automatic=true;followWindow();changed(); },
            refreshLanguage() { chartFont = resolveFont(); backgroundDirty = true; hover = null; const tip = document.querySelector('#tooltip'); if (tip) tip.hidden = true; render(); },
            setTime(value) {
                currentTime = value;
                if(followWindow()){backgroundDirty=true;clearHover();}
                render();
            },
            destroy() {
                destroyed = true;
                background = null;
                document.fonts?.removeEventListener('loadingdone', fontsReady);
                resize.disconnect();
            }
        };
    }
    return {
        theme,
        replay,
        interpolateAt, createXViewport,
        position, standings, riderState, markerColor, drawMarker
    };
})();

/* Global configuration drafts; only explicit Save writes through the local API. */
'use strict';
(() => {
    const same = (a, b) => a && typeof a === 'object' || b && typeof b === 'object' ? JSON.stringify(a) === JSON.stringify(b) : Object.is(a,b);
    const numberType = field => ['number', 'integer'].includes(field.type);
    class SettingsDraft {
        constructor() { this.drafts = new Map(); this.fields = new Map(); this.schema = null; this.course = ''; this.conflict = false; }
        id(field, course = this.course) { return field.group === 'course' ? `${course}\n${field.key}` : field.key; }
        accept(schema, course, rebase = false) {
            this.conflict = false;
            this.schema = schema; this.course = course; this.fields.clear();
            for (const group of schema.groups) for (const field of group.fields || []) {
                const entry = {...field, group:group.id}; this.fields.set(entry.key, entry);
                const id = this.id(entry), draft = this.drafts.get(id);
                if (!draft) continue;
                if (same(this.normalise(entry, draft.value), entry.value)) this.drafts.delete(id);
                else if (rebase) draft.base = entry.value;
                else if (!same(draft.base, entry.value)) this.conflict = true;
            }
        }
        normalise(field, value) {
            if (value === null && field.nullable) return null;
            if (numberType(field) && typeof value === 'string' && value.trim() !== '' && Number.isFinite(Number(value))) return Number(value);
            return value;
        }
        edit(field, value) {
            if (field.read_only || field.disabled) return;
            const id = this.id(field);
            if (same(this.normalise(field, value), field.value)) this.drafts.delete(id);
            else this.drafts.set(id, {value,base:this.drafts.has(id) ? this.drafts.get(id).base : field.value});
        }
        value(field) { return this.drafts.get(this.id(field))?.value ?? (this.drafts.has(this.id(field)) ? null : field.value); }
        displayed(field) {
            const raw = this.value(field);
            if (raw !== null) return raw;
            return this.drafts.has(this.id(field)) ? field.default_value ?? field.effective_value ?? '' : field.effective_value ?? field.default_value ?? '';
        }
        changes() {
            const changes = {};
            for (const field of this.fields.values()) if (this.drafts.has(this.id(field))) changes[field.key] = this.normalise(field, this.value(field));
            return changes;
        }
        discard() { for (const field of this.fields.values()) this.drafts.delete(this.id(field)); }
        acknowledge(submitted) {
            for (const [id, value] of submitted) if (this.drafts.has(id) && same(this.drafts.get(id).value, value)) this.drafts.delete(id);
        }
        submitted() { return new Map([...this.fields.values()].filter(field => this.drafts.has(this.id(field))).map(field => [this.id(field), this.value(field)])); }
        invalid() {
            for (const field of this.fields.values()) {
                if (!this.drafts.has(this.id(field))) continue;
                const value = this.normalise(field, this.value(field));
                if (value === null && field.nullable) continue;
                if (numberType(field)) {
                    if (value === '' || typeof value !== 'number' || !Number.isFinite(value)) return {field, message:'请输入有效数值。'};
                    if (field.type === 'integer' && !Number.isInteger(value)) return {field, message:'请输入整数。'};
                    if ((field.min != null && value < field.min) || (field.max != null && value > field.max)) return {field, message:'数值必须在 {min} 至 {max} 之间。'};
                    if (Number(field.step) > 0 && Math.abs((value - (field.min ?? 0)) / field.step - Math.round((value - (field.min ?? 0)) / field.step)) > 1e-7) return {field, message:'请按 {step} 的步长设置数值。'};
                }
                if (['boolean','bool'].includes(field.type) && typeof value !== 'boolean') return {field, message:'请检查标出的配置项。'};
                if (field.type === 'select' && !(field.options || []).some(option => same(typeof option === 'object' ? option.value : option, value))) return {field, message:'请检查标出的配置项。'};
            }
            return null;
        }
    }
    function createSettingsUI(container, {document, fetch, i18n, onSaved = () => {}}) {
        const store = new SettingsDraft(), t = i18n.t;
        const element = (tag, className = '') => { const node = document.createElement(tag); node.className = className; return node; };
        let bindings = [], controls = new Map(), course = '', loadedCourse = null, loadToken = 0, loading = false, saving = false, error = '';
        const opened = new Map();
        const heading = element('h2','sidebar-heading settings-heading'); heading.id = 'settings-title';
        const headingText = element('span'), dirtyMark = element('span','settings-dirty');
        heading.append(headingText,dirtyMark);
        const body = element('div','settings-body'), groups = element('div','settings-groups');
        const timing = element('p','settings-timing'), status = element('p','settings-status');
        status.setAttribute('role','alert'); status.hidden = true;
        const actions = element('div','settings-actions');
        const save = element('button','primary settings-save'), discard = element('button','quiet settings-discard'), reload = element('button','quiet settings-reload');
        for (const button of [save,discard,reload]) button.type = 'button';
        actions.append(save,discard,reload); body.append(groups,timing,status,actions); container.replaceChildren(heading,body);
        const text = (value, item, key = 'label') => {
            if (value && typeof value === 'object') return value[i18n.language] || value.zh || value.en || '';
            return i18n.language === 'en' && item?.[key + '_en'] ? item[key + '_en'] : t(value || '');
        };
        const bind = (node, read, attribute) => { const update = () => { if (attribute) node.setAttribute(attribute,read()); else node.textContent = read(); }; bindings.push(update); update(); };
        function refresh() {
            for (const update of bindings) update();
            headingText.textContent = t('赛事配置');
            const count = Object.keys(store.changes()).length;
            dirtyMark.textContent = count ? '•' : '';
            dirtyMark.setAttribute('aria-label',count ? t('尚有未保存的更改') : '');
            save.textContent = t(saving ? '正在保存…' : count ? '保存更改（{count}）' : '保存更改',{count});
            save.disabled = !store.schema || !count || loading || saving || store.conflict || loadedCourse !== course;
            discard.textContent = t('撤销更改'); discard.hidden = !count; discard.disabled = saving;
            reload.textContent = t('重新读取配置'); reload.hidden = !error && !store.conflict; reload.disabled = loading || saving;
            timing.textContent = text(store.schema?.timing || '各项配置按下方说明生效。');
            status.hidden = !error && !loading;
            status.textContent = loading ? t('正在读取配置…') : t(error);
            status.classList.toggle('error',!!error);
            for (const {field, input, reset, row, steps} of controls.values()) {
                input.disabled = !!field.disabled || !!field.read_only || loading || loadedCourse !== course;
                for (const step of steps) step.disabled = input.disabled;
                row.classList.toggle('is-dirty',store.drafts.has(store.id(field)));
                if (reset) reset.disabled = input.disabled || store.value(field) === null;
            }
        }
        function syncInput(field, input, note) {
            if (['boolean','bool'].includes(field.type)) input.checked = !!store.value(field);
            else if (field.type === 'select') input.value = JSON.stringify(store.value(field));
            else input.value = store.displayed(field) ?? '';
            if (note) note.hidden = store.value(field) !== null;
            input.removeAttribute('aria-invalid');
        }
        function fieldRow(source, parent, sharedHelp = '') {
            const field = store.fields.get(source.key), row = element('div','settings-row');
            row.dataset.key = field.key;
            const label = element('label','settings-label'), caption = element('span'), help = element('small');
            const input = element(field.type === 'select' ? 'select' : 'input',numberType(field) ? 'settings-number' : 'settings-input');
            input.id = 'global-setting-' + encodeURIComponent(field.key);
            const steps = [];
            bind(input,() => [text(field.label,field),t(field.unit || '')].filter(Boolean).join(' '),'aria-label');
            label.htmlFor = input.id; label.append(caption,help);
            bind(caption,() => text(field.label,field)); bind(help,() => text(field.help,field,'help')); help.hidden = !field.help || field.help === sharedHelp;
            const control = element('div','settings-control'), valueControl = element('div','settings-value');
            control.append(valueControl);
            const defaultNote = field.nullable ? element('small','settings-default') : null;
            if (defaultNote) bind(defaultNote,() => t('当前使用默认值'));
            if (['boolean','bool'].includes(field.type)) { input.type = 'checkbox'; input.setAttribute('role','switch'); }
            else if (numberType(field)) {
                input.type = 'number'; input.inputMode = 'decimal';
                if (field.min != null) input.min = field.min;
                if (field.max != null) input.max = field.max;
                input.step = field.step ?? (field.type === 'integer' ? 1 : 'any');
                const stepper = element('div','settings-stepper');
                for (const direction of [-1,1]) {
                    const button = element('button','settings-step'); button.type = 'button'; button.textContent = direction < 0 ? '−' : '+';
                    steps.push(button);
                    bind(button,() => t(direction < 0 ? '减少 {name}' : '增加 {name}',{name:text(field.label,field)}),'aria-label');
                    button.addEventListener('click',() => {
                        if (input.disabled) return;
                        const displayed = Number(store.displayed(field));
                        const baseline = Number.isFinite(displayed) ? displayed : Number(field.default_value ?? field.min ?? 0);
                        let value = Number((baseline + direction * Number(field.step ?? 1)).toFixed(12));
                        if (field.min != null) value = Math.max(field.min,value);
                        if (field.max != null) value = Math.min(field.max,value);
                        store.edit(field,value); if (!store.conflict) error = ''; syncInput(field,input,defaultNote); refresh();
                    });
                    if (direction < 0) stepper.append(button,input); else stepper.append(button);
                }
                valueControl.append(stepper);
            } else if (field.type === 'select') {
                for (const option of field.options || []) {
                    const item = typeof option === 'object' ? option : {value:option,label:String(option)};
                    const node = element('option'); node.value = JSON.stringify(item.value);
                    const shared = field.key.endsWith('.cornering') ? store.schema.groups.find(group => group.id === field.group)?.default_ability?.cornering?.value : null;
                    bind(node,() => item.value === 0 && Number.isFinite(shared) ? t('默认（{value}）',{value:shared}) : text(item.label,item)); input.append(node);
                }
            } else { row.hidden = true; return; } // Unknown schema types never become arbitrary text writes.
            if (!numberType(field)) valueControl.append(input);
            if (field.unit) { const unit = element('span','settings-unit'); bind(unit,() => t(field.unit)); valueControl.append(unit); }
            let reset;
            if (field.nullable) {
                reset = element('button','quiet settings-reset'); reset.type = 'button'; bind(reset,() => t('恢复默认'));
                reset.addEventListener('click',() => { store.edit(field,null); if (!store.conflict) error = ''; syncInput(field,input,defaultNote); refresh(); });
                control.append(reset,defaultNote);
            }
            input.addEventListener(['boolean','bool','select'].includes(field.type) ? 'change' : 'input',() => {
                const value = ['boolean','bool'].includes(field.type) ? input.checked : field.type === 'select' ? JSON.parse(input.value) : input.value;
                store.edit(field,value); if (defaultNote) defaultNote.hidden = store.value(field) !== null;
                input.removeAttribute('aria-invalid'); if (!store.conflict) error = ''; refresh();
            });
            row.append(label,control); parent.append(row); controls.set(field.key,{field,input,reset,row,steps}); syncInput(field,input,defaultNote);
        }
        function hostDefaults(group, parent) {
            const defaults = group.default_ability;
            if (!defaults?.speed || !defaults.acceleration) return;
            const box = element('div','settings-host-defaults');
            const range = rule => Number(rule.minimum) === Number(rule.maximum) ? String(rule.minimum) : `${rule.minimum}–${rule.maximum}`;
            // Keep each number and its level unit together in either locale.
            const levels = value => i18n.language === 'en' ? `${value}\u00a0${Number(value) === 1 ? 'level' : 'levels'}` : `${value}\u00a0级`;
            const bindUnits = (node, read) => {
                const update = () => node.replaceChildren(...read().split(/([+]?\d+(?:–\d+)?\u00a0(?:级|levels?))/g).filter(Boolean).map(part => {
                    const span = element('span',/[\u00a0]/.test(part) ? 'settings-ability-number' : '');
                    span.textContent = part; return span;
                }));
                bindings.push(update); update();
            };
            const upper = defaults.selection === 'upper_bound';
            const current = axis => {
                const field = suffix => store.fields.get(`${group.id}.${axis}_${suffix}`);
                const custom = field('custom') && store.value(field('custom')) === true;
                if (custom) {
                    const value = suffix => field(suffix) ? store.value(field(suffix)) : '…';
                    const rule = {minimum:value('min'),maximum:value('max')};
                    const selected = upper ? levels(rule.maximum) : levels(range(rule));
                    return t(upper ? '本场：自定义上界 {value}' : '本场：自定义范围 {value}，独立随机',{value:selected}) +
                        (axis === 'acceleration' ? t('；不叠加原生加成') : '');
                }
                const rule = defaults[axis];
                if (rule.mode === 'natural') {
                    if (!rule.bonus_maximum) return t('本场：随马原生');
                    return upper ? t('本场：随马原生 +{bonus}，最高 {cap}',{bonus:levels(rule.bonus_maximum),cap:levels(defaults.level_cap)}) :
                        t('本场：原生加成独立随机，最高 {cap}',{cap:levels(defaults.level_cap)});
                }
                return t(upper ? '本场：默认上界 {value}' : rule.minimum === rule.maximum ? '本场：默认 {value}' : '本场：默认范围 {value}，独立随机',
                    {value:levels(upper ? rule.maximum : range(rule))});
            };
            const add = (axis,label) => {
                const row = element('div','settings-ability-row'), heading = element('div','settings-ability-heading');
                row.dataset.ability = axis;
                const caption = element('span'), value = element('span','settings-ability-value'), note = element('small','settings-ability-current');
                bind(caption,() => t(label));
                bindUnits(value,() => {
                    const rule = defaults[axis];
                    if (rule.mode !== 'natural') return levels(range(rule));
                    return rule.bonus_maximum ? t('随马原生 +{bonus}',{bonus:levels(range({minimum:rule.bonus_minimum,maximum:rule.bonus_maximum}))}) : t('随马原生');
                });
                bindUnits(note,() => current(axis)); heading.append(caption,value); row.append(heading,note); box.append(row);
            };
            add('speed','默认速度'); add('acceleration','默认加速度'); parent.append(box);
        }
        function render() {
            bindings = []; controls.clear(); groups.replaceChildren();
            if (!store.schema) { refresh(); return; }
            const commonHelp = group => {
                const counts = new Map();
                for (const field of group.fields || []) if (field.help) counts.set(field.help,(counts.get(field.help) || 0) + 1);
                return [...counts].sort((a,b) => b[1]-a[1]).find(([,count]) => count > 1)?.[0] || '';
            };
            function groupPanel(group, defaultOpen = false) {
                const detail = element('details','settings-group'); detail.open = opened.has(group.id) ? opened.get(group.id) : defaultOpen;
                const title = element('summary'); bind(title,() => text(group.label,group)); detail.append(title);
                if (group.description) { const note = element('p','settings-group-description'); bind(note,() => text(group.description,group,'description')); detail.append(note); }
                if (group.id.startsWith('npc.')) hostDefaults(group,detail);
                const sharedHelp = commonHelp(group);
                if (sharedHelp && sharedHelp !== group.description && sharedHelp !== '下一场比赛生效') { const note = element('p','settings-group-description'); bind(note,() => t(sharedHelp)); detail.append(note); }
                const fields = element('div','settings-fields'); detail.append(fields); groups.append(detail);
                detail.addEventListener('toggle',() => opened.set(group.id,detail.open)); return {detail,fields,sharedHelp};
            }
            for (const group of store.schema.groups) {
                const view = groupPanel(group,true);
                for (const field of group.fields || []) fieldRow(field,view.fields,view.sharedHelp);
            }
            refresh();
        }
        const validSchema = data => Array.isArray(data?.groups) && Object.hasOwn(data,'revision');
        async function request(url, options) {
            const response = await fetch(url,{cache:'no-store',...options});
            let data;
            try { data = await response.json(); } catch { throw Error(t('无法读取配置。')); }
            if (!response.ok) { const error = Error(data.error || t('无法保存配置。')); error.status = response.status; throw error; }
            return data;
        }
        async function load(rebase = false) {
            const token = ++loadToken, requestedCourse = course; loading = true; error = ''; refresh();
            try {
                const data = await request('/api/settings?course_id=' + encodeURIComponent(requestedCourse));
                if (token !== loadToken) return;
                if (!validSchema(data)) throw Error(t('无法读取配置。'));
                store.accept(data,requestedCourse,rebase); loadedCourse = requestedCourse;
                if (store.conflict) error = '配置已在其他位置更改。重新读取后检查草稿再保存。';
                render();
            } catch (failure) { if (token === loadToken) error = failure.message; }
            finally { if (token === loadToken) { loading = false; refresh(); } }
        }
        async function saveDraft() {
            const changes = store.changes();
            if (saving || loading || !Object.keys(changes).length || store.conflict || loadedCourse !== course) return;
            const invalid = store.invalid();
            if (invalid) {
                opened.set(invalid.field.group,true);
                render(); const field = controls.get(invalid.field.key);
                field?.input.setAttribute('aria-invalid','true'); field?.input.focus();
                error = t(invalid.message,{min:invalid.field.min,max:invalid.field.max,step:invalid.field.step}); refresh(); return;
            }
            const requestedCourse = course, submitted = store.submitted();
            saving = true; error = ''; refresh();
            try {
                const data = await request('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({revision:store.schema.revision,course_id:requestedCourse,changes})});
                if (!validSchema(data)) throw Error(t('无法保存配置。'));
                store.acknowledge(submitted);
                if (course === requestedCourse) { store.accept(data,requestedCourse,true); loadedCourse = course; render(); }
                else await load(true);
                onSaved({revision:data.revision,course_id:requestedCourse,changes});
            } catch (failure) {
                store.conflict = failure.status === 409;
                error = failure.status === 409 ? '配置已在其他位置更改。重新读取后检查草稿再保存。' : failure.message;
            } finally { saving = false; refresh(); }
        }
        save.addEventListener('click',saveDraft);
        discard.addEventListener('click',() => { store.discard(); if (!store.conflict) error = ''; render(); });
        reload.addEventListener('click',() => load(true));
        const unsubscribe = i18n.onChange(refresh);
        refresh();
        return {setCourse(id) { id = String(id || ''); if (course === id) return; course = id; return load(); },load,save:saveDraft,destroy() { unsubscribe(); ++loadToken; }};
    }
    if (typeof module !== 'undefined' && module.exports) module.exports = {SettingsDraft,createSettingsUI};
    if (typeof window !== 'undefined' && typeof document !== 'undefined') {
        const container = document.querySelector('#global-settings');
        if (container) window.RaceSettings = createSettingsUI(container,{document,fetch:window.fetch.bind(window),i18n:window.RaceI18n,onSaved:detail => window.dispatchEvent(new CustomEvent('race-settings-saved',{detail}))});
    }
})();

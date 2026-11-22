'use strict';

window.HorseUI = (() => {
    const dictionary = {
        "未记录": "Not recorded",
        "生命": "Health",
        "体力": "Stamina",
        "默契": "Bonding",
        "默契等级": "Bonding level",
        "玩家": "Player",
        "主办人": "Host",
        "骑手": "Rider",
        "参赛马匹": "Horses",
        "速度": "Speed",
        "加速度": "Acceleration",
        "开赛状态": "At race start",
        "其他骑手": "Other riders",
        "选择骑手…": "Select rider…",
        "实体生命 · 量表200": "Entity health · Scale 200",
        "生命等级": "Health level",
        "体力等级": "Stamina level",
        "实体 {value}": "Entity {value}",
        "体力储备 · 量表{scale}": "Stamina reserve · Scale {scale}"
};
    window.RaceI18n?.register(dictionary);
    const t = (source, values = {}) => window.RaceI18n?.t(source, values) ?? source.replace(/\{(\w+)\}/g, (_, key) => values[key] ?? `{${key}}`);

    // Native installed American/Simplified-Chinese global.yldb tables, matched by GXT hash.
    // Provenance: analysis/horse-localization-20261023/{breed,coat}-matches.json.
    // Custom horse names remain verbatim; unknown catalog entries do not leak another locale.
    const breedNames = {"American Paint": "美国花马", "American Standardbred": "美国标准种马", "Andalusian": "安达卢西亚马", "Appaloosa": "阿帕卢萨马", "Arabian": "阿拉伯马", "Ardennes": "阿尔登马", "Belgian": "比利时马", "Breton": "布列塔尼马", "Criollo": "克里奥尔马", "Dutch Warmblood": "荷兰温血马", "Gypsy Cob": "吉普赛马", "Hungarian Halfbred": "匈牙利混种马", "Kentucky Saddler": "肯塔基骑乘马", "Kladruber": "克拉德鲁伯马", "Missouri Fox Trotter": "密苏里狐步马", "Morgan": "摩根马", "Mustang": "美洲野马", "Nokota": "北达科他马", "Norfolk Roadster": "诺福克赛马", "Shire": "夏尔马", "Suffolk Punch": "萨福克马", "Tennessee Walker": "田纳西走马", "Thoroughbred": "纯血马", "Turkoman": "土库曼马"};
    const coatNames = {"Albino": "白化色", "Amber Champagne": "琥珀香槟色", "Bay": "骝色", "Bay Brindle": "骝色斑纹", "Bay Frame Overo": "骝色白斑欧维欧", "Bay Roan": "骝沙", "Black": "黑色", "Black Chestnut": "黑栗色", "Black Overo": "黑色欧维欧", "Black Snowflake": "黑色雪花状", "Black Tovero": "黑色托维欧", "Blanket": "毯斑", "Blond Chestnut": "浅金栗", "Blood Bay": "血骝色", "Blue Roan": "蓝沙色", "Blue Roan Overo": "蓝沙色欧维欧", "Brindle": "斑纹", "Brown Leopard": "棕色豹斑", "Brown Roan": "红棕沙色", "Buckskin": "鹿皮棕色", "Buckskin Brindle": "鹿皮棕色斑纹", "Buttermilk Buckskin": "酪乳鹿皮棕色", "Chestnut": "栗色", "Chestnut Pinto": "栗色花斑", "Chestnut Tovero": "栗色托维欧", "Chocolate Roan": "巧克力沙色", "Cremello": "象牙白色", "Dapple Bay": "骝色斑纹", "Dapple Grey": "灰色斑纹", "Dapple Rose Grey": "花纹玫瑰灰色", "Dappled Buckskin": "斑纹鹿皮棕色", "Dark Bay": "深骝色", "Dark Bay Roan": "深骝沙色", "Dark Dapple Grey": "深灰色斑纹", "Dun": "暗褐色", "Few Spotted": "稀疏斑点", "Flaxen Chestnut": "浅栗色", "Flaxen Roan": "浅沙色", "Gold": "黄金", "Gold Dapple": "金色斑纹", "Gold Palomino": "金帕洛米诺", "Golden Dun": "黄金暗褐色", "Grey": "格雷家族", "Grey Overo": "灰色欧维欧", "Grey Roan Sabino": "灰沙色萨比诺斑", "Grey Snowcap Spotted": "灰雪花斑", "Grullo Dun": "格鲁洛暗褐色", "Iron Grey Roan": "铁灰沙色", "Leopard": "豹斑", "Leopard Blanket": "豹纹毯斑", "Light Buckskin": "浅鹿皮棕色", "Light Grey": "浅灰", "Light Palomino": "浅色帕洛米诺", "Liver Chestnut": "棕栗色", "Marble Sabino": "大理石萨比诺斑", "Mealy Chestnut": "粉状斑栗色", "Mealy Dapple Bay": "粉状条纹骝色", "Overo": "欧维欧", "Palomino": "帕洛米诺", "Palomino Blagdon": "帕洛米诺布莱格登", "Palomino Dapple": "帕洛米诺斑纹", "Perlino": "浅粉色", "Piebald": "花斑", "Piebald Roan": "花斑沙色", "Piebald Tobiano": "托比亚诺斑", "Raven Black": "漆黑", "Red Chestnut": "红栗", "Red Dun Overo": "红褐色欧维欧", "Red Roan": "红沙色", "Reverse Dapple Black": "反转黑色斑纹", "Reverse Dapple Roan": "反转沙色斑纹", "Rose Gray": "玫瑰灰色", "Rose Grey": "玫瑰灰色", "Rose Grey Bay": "玫瑰灰骝色", "Sabino": "萨比诺斑", "Sable Champagne": "黑貂香槟色", "Seal Brown": "深褐色", "Silver": "白银", "Silver Bay": "银骝色", "Silver Dapple Pinto": "银色花斑", "Silver Dark Bay": "银尾深骝色", "Silver Tail Buckskin": "银尾鹿皮棕色", "Skewbald": "雅斑", "Smoky Black": "烟黑色", "Sooty Buckskin": "烟熏鹿皮棕色", "Sorrel": "栗色", "Sorrel Overo": "栗色欧维欧", "Speckled Grey": "斑点灰色", "Splashed Bay": "斑驳骝色", "Splashed Piebald": "斑驳花斑", "Splashed White": "白斑", "Spotted Tricolor": "花斑三色", "Steel Grey": "钢灰色", "Strawberry Roan": "草莓沙色", "Tiger Striped Bay": "虎纹骝色", "Tobiano": "托比亚诺", "Warped Brindle": "弯曲斑", "White": "白色", "White Blagdon": "白布莱格登", "White Roan": "白沙色", "Wild Bay": "自然骝色", "Dapple Gray": "灰色斑纹", "Dapple Rose Gray": "花纹玫瑰灰色", "Dapple Dark Gray": "深灰色斑纹", "Gray": "格雷家族", "Gray Overo": "灰色欧维欧", "Gray Roan Sabino": "灰沙色萨比诺斑", "Gray Snowcap Spotted": "灰雪花斑", "Iron Gray Roan": "铁灰沙色", "Light Gray": "浅灰", "Rose Gray Bay": "玫瑰灰骝色", "Speckled Gray": "斑点灰色", "Steel Gray": "钢灰色"};
    const coatEnglish = {"Albino": "Albino", "Amber Champagne": "Amber Champagne", "Bay": "Bay", "Bay Brindle": "Bay Brindle", "Bay Frame Overo": "Bay Frame Overo", "Bay Roan": "Bay Roan", "Black": "Black", "Black Chestnut": "Black Chestnut", "Black Overo": "Black Overo", "Black Snowflake": "Black Snowflake", "Black Tovero": "Black Tovero", "Blanket": "Blanket", "Blond Chestnut": "Blond Chestnut", "Blood Bay": "Blood Bay", "Blue Roan": "Blue Roan", "Blue Roan Overo": "Blue Roan Overo", "Brindle": "Brindle", "Brown Leopard": "Brown Leopard", "Brown Roan": "Brown Roan", "Buckskin": "Buckskin", "Buckskin Brindle": "Buckskin Brindle", "Buttermilk Buckskin": "Buttermilk Buckskin", "Chestnut": "Chestnut", "Chestnut Pinto": "Chestnut Pinto", "Chestnut Tovero": "Chestnut Tovero", "Chocolate Roan": "Chocolate Roan", "Cremello": "Cremello", "Dapple Bay": "Dapple Bay", "Dapple Grey": "Dapple Gray", "Dapple Rose Grey": "Dapple Rose Gray", "Dappled Buckskin": "Dappled Buckskin", "Dark Bay": "Dark Bay", "Dark Bay Roan": "Dark Bay Roan", "Dark Dapple Grey": "Dapple Dark Gray", "Dun": "Dun", "Few Spotted": "Few Spotted", "Flaxen Chestnut": "Flaxen Chestnut", "Flaxen Roan": "Flaxen Roan", "Gold": "Gold", "Gold Dapple": "Gold Dapple", "Gold Palomino": "Gold Palomino", "Golden Dun": "Golden Dun", "Grey": "Gray", "Grey Overo": "Gray Overo", "Grey Roan Sabino": "Gray Roan Sabino", "Grey Snowcap Spotted": "Gray Snowcap Spotted", "Grullo Dun": "Grullo Dun", "Iron Grey Roan": "Iron Gray Roan", "Leopard": "Leopard", "Leopard Blanket": "Leopard Blanket", "Light Buckskin": "Light Buckskin", "Light Grey": "Light Gray", "Light Palomino": "Light Palomino", "Liver Chestnut": "Liver Chestnut", "Marble Sabino": "Marble Sabino", "Mealy Chestnut": "Mealy Chestnut", "Mealy Dapple Bay": "Mealy Dapple Bay", "Overo": "Overo", "Palomino": "Palomino", "Palomino Blagdon": "Palomino Blagdon", "Palomino Dapple": "Palomino Dapple", "Perlino": "Perlino", "Piebald": "Piebald", "Piebald Roan": "Piebald Roan", "Piebald Tobiano": "Piebald Tobiano", "Raven Black": "Raven Black", "Red Chestnut": "Red Chestnut", "Red Dun Overo": "Red Dun Overo", "Red Roan": "Red Roan", "Reverse Dapple Black": "Reverse Dapple Black", "Reverse Dapple Roan": "Reverse Dapple Roan", "Rose Gray": "Rose Gray", "Rose Grey": "Rose Gray", "Rose Grey Bay": "Rose Gray Bay", "Sabino": "Sabino", "Sable Champagne": "Sable Champagne", "Seal Brown": "Seal Brown", "Silver": "Silver", "Silver Bay": "Silver Bay", "Silver Dapple Pinto": "Silver Dapple Pinto", "Silver Dark Bay": "Silver Dark Bay", "Silver Tail Buckskin": "Silver Tail Buckskin", "Skewbald": "Skewbald", "Smoky Black": "Smoky Black", "Sooty Buckskin": "Sooty Buckskin", "Sorrel": "Sorrel", "Sorrel Overo": "Sorrel Overo", "Speckled Grey": "Speckled Gray", "Splashed Bay": "Splashed Bay", "Splashed Piebald": "Splashed Piebald", "Splashed White": "Splashed White", "Spotted Tricolor": "Spotted Tricolor", "Steel Grey": "Steel Gray", "Strawberry Roan": "Strawberry Roan", "Tiger Striped Bay": "Tiger Striped Bay", "Tobiano": "Tobiano", "Warped Brindle": "Warped Brindle", "White": "White", "White Blagdon": "White Blagdon", "White Roan": "White Roan", "Wild Bay": "Wild Bay"};
    const catalogName = (value, names) => !value ? value : window.RaceI18n?.language === 'zh'
        ? (names[value] || t('未记录')) : names === coatNames ? (coatEnglish[value] || value) : value;
    const valid = value => value !== null && value !== undefined && Number.isFinite(Number(value));
    const number = value => valid(value) ? Number(value).toFixed(Number.isInteger(Number(value)) ? 0 : 2) : '—';
    const clamp = value => Math.max(0, Math.min(1, value));

    const node = (tag, className, text) => {
        const element = document.createElement(tag);
        if (className) element.className = className;
        if (text !== undefined) element.textContent = text;
        return element;
    };

    // Entity health is not a breed statistic. Every entity-health ring uses 200.
    function capacityModel(attribute, key) {
        attribute = attribute || {};
        const level = attribute.level ?? attribute.rank;
        if (valid(level)) {
            return {
                ratio: clamp(Number(level) / 10),
                value: `${number(level)} / 10`,
                note: t(key === 'health' ? '生命等级' : '体力等级'),
                actual: valid(attribute.value) ? t('实体 {value}', {value: `${number(attribute.value)}${valid(attribute.max) ? ' / '+number(attribute.max) : ''}`}) : null
            };
        }
        if (!valid(attribute.value)) return {
            ratio: null,
            value: t('未记录'),
            note: '',
            actual: null
        };
        const scale = key === 'health' ? 200 : Number(attribute.max) === 1 ? 1 : 100;
        return {
            ratio: clamp(Number(attribute.value) / scale),
            value: `${number(attribute.value)}${valid(attribute.max) ? ' / '+number(attribute.max) : ''}`,
            note: key === 'health' ? t('实体生命 · 量表200') : t('体力储备 · 量表{scale}', {scale}),
            actual: null
        };
    }

    function svgElement(tag, attributes) {
        const element = document.createElementNS('http://www.w3.org/2000/svg', tag);
        for (const [key, value] of Object.entries(attributes || {})) element.setAttribute(key, String(value));
        return element;
    }

    function ring(key, ratio, bonding) {
        const svg = svgElement('svg', {
            viewBox: '0 0 64 64',
            class: 'horse-state-svg',
            'aria-hidden': 'true'
        });
        const fullPath = 'M32 6 A26 26 0 1 1 32 58 A26 26 0 1 1 32 6';
        if (key === 'bonding') {
            for (let index = 0; index < 4; index++) {
                const angle = degree => ({
                    x: 32 + 26 * Math.cos(degree * Math.PI / 180),
                    y: 32 + 26 * Math.sin(degree * Math.PI / 180)
                });
                const start = angle(-90 + index * 90),
                    end = angle(-90 + index * 90 + 84);
                svg.append(svgElement('path', {
                    d: `M${start.x} ${start.y} A26 26 0 0 1 ${end.x} ${end.y}`,
                    class: valid(bonding) && index < bonding ? 'horse-state-fill' : 'horse-state-track'
                }));
            }
        } else {
            svg.append(svgElement('path', {
                d: fullPath,
                class: 'horse-state-track'
            }));
            if (ratio !== null) svg.append(svgElement('path', {
                d: fullPath,
                class: 'horse-state-fill',
                pathLength: 100,
                'stroke-dasharray': `${ratio*100} 100`
            }));
        }
        return svg;
    }

    function vital(horse, key, label) {
        const attribute = horse?.attributes?.[key] || {};
        const item = node('div', 'horse-vital');
        item.dataset.attribute = key;
        let model;
        if (key === 'bonding') {
            const value = valid(attribute.value) && Number.isInteger(Number(attribute.value)) && attribute.value >= 0 && attribute.value <= 4 ? Number(attribute.value) : null;
            model = {
                ratio: value === null ? null : value / 4,
                value: value === null ? t('未记录') : `${number(value)} / 4`,
                note: t('默契等级')
            };
        } else model = capacityModel(attribute, key);
        item.append(node('span', 'horse-vital-label', label));
        const icon = node('span', 'horse-vital-icon');
        icon.append(ring(key, model.ratio, key === 'bonding' && model.ratio !== null ? model.ratio * 4 : null));

        item.append(icon, node('b', 'horse-vital-value', model.value));
        const english = window.RaceI18n?.language === 'en';
        const explanation = `${label}${english ? ': ' : '：'}${[model.value, model.note, model.actual].filter(Boolean).join(english ? ', ' : '，')}`;
        item.title = explanation;
        item.setAttribute('aria-label', explanation);
        return item;
    }

    function horseCard(horse, role) {
        const card = node('article', 'horse-card');
        card.dataset.role = role;
        const identity = node('div', 'horse-identity');
        const roleName = role === 'player' ? t('玩家') : `${role === 'host' ? t('主办人') : t('骑手')}${horse?.name ? ' '+horse.name : ''}`;
        identity.append(node('span', 'horse-role', roleName), node('h3', '', horse?.horse_name || catalogName(horse?.breed, breedNames) || t('参赛马匹')));
        if (horse?.coat) identity.append(node('p', 'horse-meta', catalogName(horse.coat, coatNames)));
        card.append(identity);
        const vitals = node('div', 'horse-vitals');
        for (const [key, label] of [
                ['health', t('生命')],
                ['stamina', t('体力')],
                ['bonding', t('默契')]
            ]) vitals.append(vital(horse, key, label));
        card.append(vitals);
        for (const [key, label] of [
                ['speed', t('速度')],
                ['acceleration', t('加速度')]
            ]) {
            const attribute = horse?.attributes?.[key] || {};
            const value = valid(attribute.value) ? Number(attribute.value) : null;
            const row = node('div', 'horse-attribute');
            row.dataset.attribute = key;
            const bars = node('span', 'attribute-bars');
            bars.setAttribute('aria-hidden', 'true');
            const base = valid(attribute.base) && valid(attribute.bonus) ? Math.min(value, Math.max(0, attribute.base)) : value;
            for (let index = 0; index < 10; index++) bars.append(node('i', value !== null && index < value ? index >= base ? 'filled bonus' : 'filled' : ''));
            row.append(node('span', '', label), bars, node('b', 'horse-rank-value', value === null ? t('未记录') : `${number(value)}/10`));
            card.append(row);
        }
        return card;
    }

    function horses(container, data, subscribe = true) {
        const section = node('section', 'horse-section horse-ui');
        const heading = node('div', 'section-heading');
        heading.append(node('h2', '', t('参赛马匹')), node('span', '', t('开赛状态')));
        section.append(heading);
        const list = Array.isArray(data?.horses) ? data.horses : [];
        const grid = node('div', 'horse-grid');
        grid.append(horseCard(list.find(h => h.role === 'player'), 'player'), horseCard(list.find(h => h.role === 'host'), 'host'));
        section.append(grid);
        const jockeys = list.filter(h => !['player', 'host'].includes(h.role));
        if (jockeys.length) {
            const label = node('label', 'jockey-picker', t('其他骑手'));
            const picker = node('select');
            picker.append(new Option(t('选择骑手…'), ''));
            jockeys.forEach((horse, index) => picker.append(new Option(horse.name || `Jockey ${index+1}`, String(index))));
            const details = node('div', 'jockey-details');
            picker.onchange = () => {
                details.replaceChildren();
                if (picker.value !== '') details.append(horseCard(jockeys[Number(picker.value)], 'jockey'));
            };
            label.append(picker);
            section.append(label, details);
        }
        container.append(section);
        let unsubscribe;
        section.destroy = () => { unsubscribe?.(); unsubscribe = null; };
        if (subscribe && window.RaceI18n?.onChange) {
            unsubscribe = window.RaceI18n.onChange(() => {
                if (section.isConnected === false) { section.destroy(); return; }
                const selected = section.querySelector('select')?.value;
                const replacement = horses({append() {}}, data, false);
                const picker = replacement.querySelector('select');
                if (picker && selected !== undefined) { picker.value = selected; picker.onchange(); }
                section.replaceChildren(...replacement.childNodes);
            });
        }
        return section;
    }
    return {
        horses,
        capacityModel
    };
})();

"""Presentation names shared by the course selector and historical race sheets.

Host/breed spellings follow src/horse_market_catalog.h. Course IDs, routes,
historical rider evidence and game configuration are never rewritten here.
"""
HOSTS = {
    'MaryBeth': ('Mary-Beth', '玛丽贝思'), 'Sadie': ('Sadie', '莎迪'),
    'Charles': ('Charles', '查尔斯'), 'Uncle': ('Uncle', '大叔'),
    'Karen': ('Karen', '凯伦'), 'Pearson': ('Pearson', '皮尔逊'),
    'Arthur': ('Arthur', '亚瑟'), 'John': ('John', '约翰'),
    'Trelawny': ('Trelawny', '特里劳尼'), 'RainsFall': ('Rains Fall', '落雨'),
    'Tilly': ('Tilly', '蒂莉'), 'Javier': ('Javier', '哈维尔'), 'Bill': ('Bill', '比尔'),
}
BREEDS = {
    'KentuckySaddler': ('Kentucky Saddler', '肯塔基骑乘马'),
    'Mustang': ('Mustang', '美洲野马'), 'Thoroughbred': ('Thoroughbred', '纯血马'),
    'Arabian': ('Arabian', '阿拉伯马'), 'AmericanStandardbred': ('Standardbred', '标准种马'),
    'AmericanPaint': ('Paint', '花马'), 'TennesseeWalker': ('Tennessee Walker', '田纳西走马'),
    'Appaloosa': ('Appaloosa', '阿帕卢萨马'), 'MissouriFoxTrotter': ('Foxtrotter', '狐步马'),
}


def host_names(host):
    raw = str(host or '').strip()
    for key, names in HOSTS.items():
        if raw.casefold() in (key.casefold(), names[0].casefold(), names[1]):
            return names
    if not raw or raw in ('赛事 Host', '未确认 Host', 'Race host'):
        return 'Rider', '骑手'
    return raw, raw


def race_names(host='', breed=''):
    if breed:
        en, zh = BREEDS.get(breed, (breed, breed))
        return {'title': zh + '限定赛', 'title_en': en + ' Challenge'}
    en, zh = host_names(host)
    return {'title': zh + '对决赛', 'title_en': en + "'s Duel"}

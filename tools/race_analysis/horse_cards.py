"""Horse Info data from the recorded instance, never today's configured ranks.

The stock source tables resolve model hashes for display only. A missing name,
bonding level or handling stat is left missing; entity health is not a breed rank.
"""
from functools import lru_cache
from pathlib import Path
import json
import re
from clubhouse_runtime import payload_root

SRC = Path(__file__).resolve().parents[2] / "src"
BREEDS = {
    "AMERICANPAINT": "American Paint", "AMERICANSTANDARDBRED": "American Standardbred",
    "ANDALUSIAN": "Andalusian", "APPALOOSA": "Appaloosa", "ARABIAN": "Arabian",
    "ARDENNES": "Ardennes", "BELGIAN": "Belgian", "BRETON": "Breton", "CRIOLLO": "Criollo",
    "DUTCHWARMBLOOD": "Dutch Warmblood", "GYPSYCOB": "Gypsy Cob",
    "HUNGARIANHALFBRED": "Hungarian Halfbred", "KENTUCKYSADDLE": "Kentucky Saddler",
    "KLADRUBER": "Kladruber", "MISSOURIFOXTROTTER": "Missouri Fox Trotter", "MORGAN": "Morgan",
    "MUSTANG": "Mustang", "NOKOTA": "Nokota", "NORFOLKROADSTER": "Norfolk Roadster",
    "SHIRE": "Shire", "SUFFOLKPUNCH": "Suffolk Punch", "TENNESSEEWALKER": "Tennessee Walker",
    "THOROUGHBRED": "Thoroughbred", "TURKOMAN": "Turkoman",
}
COATS = {
    "SEALBR": "Seal Brown", "CHEMGOLD": "Gold Dapple", "GREYSNOWCAPSPOTTED": "Grey Snowcap Spotted",
    "GREYROANSABINO": "Grey Roan Sabino", "GOLDDAP": "Gold Dapple", "BROWNR": "Brown Roan",
    "FLAXR": "Flaxen Roan", "BRLEOP": "Brown Leopard", "RAVBLACK": "Raven Black",
}
WORDS = sorted(("reverse reversed dapple dappled grey gray blood bay black chestnut seal brown silver brindle "
                "tovero overo tobiano pinto dark light buckskin tiger striped grullo dun wild gold golden "
                "white snowflake blanket leopard few spotted roan sorrel red blue flaxen liver piebald "
                "rose smoky splashed palomino amber champagne sable mealy buttermilk chocolate sooty "
                "warped raven steel tail strawberry iron blond cremello perlino marble sabino frame "
                "blagdon skewbald speckled tricolor albino snowcap".split()), key=len, reverse=True)


def joaat(text):
    value = 0
    for byte in text.lower().encode("ascii"):
        value = (value + byte) & 0xffffffff
        value = (value + (value << 10)) & 0xffffffff
        value ^= value >> 6
    value = (value + (value << 3)) & 0xffffffff
    value ^= value >> 11
    return (value + (value << 15)) & 0xffffffff


@lru_cache(maxsize=256)
def coat_text(token):
    token = token.removeprefix("COAT_").removesuffix("_PC")
    if token in COATS:
        return COATS[token]
    if not token:
        return ""
    for word in WORDS:
        if token.startswith(word.upper()):
            rest = coat_text(token[len(word):])
            if rest is not None:
                return (word.title() + " " + rest).strip()
    return None


@lru_cache(maxsize=1)
def model_catalog():
    payload = payload_root()
    if payload:
        data = json.loads((payload / "data" / "horse-models.json").read_text(encoding="utf-8"))
        return {int(key): value for key, value in data.items()}
    pool = (SRC / "fixed_horse_pool.h").read_text(encoding="utf-8")
    story = (SRC / "horse_story_support.h").read_text(encoding="utf-8")
    identity = (SRC / "horse_identity.h").read_text(encoding="utf-8")
    names = set(re.findall(r'"(A_C_HORSE_[A-Z0-9_]+)"', pool + story))
    names.add("A_C_HORSE_WINTER02_01")
    named = dict(re.findall(r'\{"([A-Z0-9_]+)","([A-Z0-9_]+)"\}', identity))
    labels = dict(re.findall(r'\{"(A_C_HORSE_[A-Z0-9_]+)","[A-Z0-9_]+","(COAT_[A-Z0-9_]+)"\}', story))
    result = {}
    for model in names:
        suffix = model.removeprefix("A_C_HORSE_")
        breed, _, coat = suffix.partition("_")
        if suffix in named:
            breed, coat = named[suffix], labels.get(model, "")
        if model == "A_C_HORSE_WINTER02_01":
            breed, coat = "THOROUGHBRED", "SILVERBAY"
        # These identities can be repurposed by providers. The old motion schema
        # did not record the effective compendium, so do not guess its identity.
        if model in ("A_C_HORSE_MP_MANGY_BACKUP", "A_C_HORSE_ARABIAN_REDCHESTNUT"):
            breed, coat = "", ""
        result[joaat(model)] = {"model_name": model, "breed": BREEDS.get(breed),
                               "coat": coat_text(coat) or None}
    return result


def describe(snapshot, actor_id, label, role):
    snapshot = snapshot or {}
    model = snapshot.get("identity", {}).get("model")
    identity = model_catalog().get(model, {})
    return {"id": actor_id, "role": role, "name": label, "horse_name": None,
            "model": model, "model_name": identity.get("model_name"),
            "breed": identity.get("breed"), "coat": identity.get("coat"),
            "attributes": snapshot.get("attributes", {}), "at_t": snapshot.get("t")}

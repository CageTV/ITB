"""ITB Birds M1: writes the Spriggit yaml for the bird records (own records, vanilla critter graph).
Run from E:/WorkSpace/ITB:  python Birds/make_records.py
Templates: ITB_hummingbirdrace (flier race layout) and ITB_pigeon (simple NPC)."""
import os,re
Y='Source/yaml'
BS=chr(92)
def rd(p): return open(p,'rb').read().decode('utf-8').replace('\r\n','\n')
def wr(p,t): open(p,'wb').write(t.replace('\n','\r\n').encode('utf-8'))
def find(d,name): return [f for f in os.listdir(f'{Y}/{d}') if f.startswith(name+' - ')][0]
IDS=dict(skin=0x1640,race={'pigeon':0x1641,'sparrow':0x1642,'crow':0x1643,'dove':0x1647,'starling':0x1648,'robin':0x164B},npc={'pigeon':0x1644,'sparrow':0x1645,'crow':0x1646,'dove':0x1649,'starling':0x164A,'robin':0x164C},ref={'pigeon':0x1650,'sparrow':0x1651,'crow':0x1652})
H=lambda v:'%06X'%v
SPEC={'pigeon':dict(name='Pigeon',height=1.0,obnd=('-8, -8, 0','8, 8, 14')),
      'sparrow':dict(name='Sparrow',height=0.55,obnd=('-5, -5, 0','5, 5, 8')),
      'crow':dict(name='Crow',height=1.35,obnd=('-10, -10, 0','10, 10, 18')),
      'dove':dict(name='Dove',height=0.95,obnd=('-8, -8, 0','8, 8, 13')),
      'starling':dict(name='Starling',height=0.7,obnd=('-6, -6, 0','6, 6, 10')),
      'robin':dict(name='Robin',height=0.6,obnd=('-5, -5, 0','5, 5, 8'))}
# --- null skin (an armor with nothing to draw: the bird mesh is part of its skeleton nif)
skin=f"""FormKey: {H(IDS['skin'])}:ITB_Wildlife.esp
MajorRecordFlagsRaw: 4
EditorID: ITB_BirdNullSkin
SkyrimMajorRecordFlags:
- NotPlayable
Name:
  TargetLanguage: English
  Value: Bird
BodyTemplate:
  FirstPersonFlags:
  - Body
  ArmorType: Clothing
  ActsLike44: True
Description:
  TargetLanguage: English
  Value: ''
MajorFlags:
- NonPlayable
"""
wr(f'{Y}/Armors/ITB_BirdNullSkin - {H(IDS["skin"])}_ITB_Wildlife.esp.yaml',skin)
# --- races
rt=rd(f'{Y}/Races/'+find('Races','ITB_hummingbirdrace'))
def race(sp):
    t=rt; s=SPEC[sp]; rid=H(IDS['race'][sp])
    t=re.sub(r'^FormKey: \S+',f'FormKey: {rid}:ITB_Wildlife.esp',t,count=1,flags=re.M)
    t=re.sub(r'^EditorID: .*$',f'EditorID: ITB_Bird{s["name"]}Race',t,count=1,flags=re.M)
    t=re.sub(r'(?m)^(Name:\n  TargetLanguage: English\n  Value: ).*$',lambda m:m.group(1)+s['name'],t,count=1)
    t=re.sub(r'(?m)^ActorEffect:\n(?:- .*\n)+','',t)
    t=re.sub(r'(?m)^Skin: .*$',f'Skin: {H(IDS["skin"])}:ITB_Wildlife.esp',t)
    t=re.sub(r'(?m)^Height:\n  Male: \S+\n  Female: \S+',"Height:\n  Male: 1\n  Female: 1",t)
    t=re.sub(r'(?m)^Flags:\n(?:- .*\n)+','Flags:\n- Flies\n- Walks\n- NotPunishable\n- NoCombatInWater\n- DontShowBloodDecal\n- CantOpenDoors\n- DontShowWeaponBlood\n',t)
    t=re.sub(r'(?m)^(- Key: Health\n  Value: )125$',lambda m:m.group(1)+'20',t,count=1)
    t=re.sub(r'(?m)^MountData:\n(?:  .*\n)+','',t)
    t=re.sub(r'(?m)^Attacks:\n(?:(?:- |  ).*\n)+','',t)
    t=re.sub(r'(?m)^BodyData:\n(?:(?:  |- ).*\n)+','',t)
    t=re.sub(r'(?m)^BodyPartData: .*\n','',t)
    nif=BS.join(['itb','fauna','birds',f'itb_bird_{sp}.nif'])
    t=re.sub(r'(?m)^(SkeletalModel:\n  Male:\n    MutagenObjectType: SimpleModel\n    File: ).*(\n    Data: .*\n  Female:\n    MutagenObjectType: SimpleModel\n    File: ).*(\n)',lambda m:m.group(1)+nif+m.group(2)+nif+m.group(3),t)
    g='Critters'+BS+'SmallBird01.hkx'
    t=re.sub(r'(?m)^(BehaviorGraph:\n  Male:\n    File: ).*(\n    Data: .*\n  Female:\n    File: ).*(\n)',lambda m:m.group(1)+g+m.group(2)+g+m.group(3),t)
    assert nif in t and g in t and 'ChaurusFlyer' not in t.replace('Dawnguard',''),sp
    return t
for sp in SPEC:
    wr(f'{Y}/Races/ITB_Bird{SPEC[sp]["name"]}Race - {H(IDS["race"][sp])}_ITB_Wildlife.esp.yaml',race(sp))
# --- NPCs
nt=rd(f'{Y}/Npcs/'+find('Npcs','ITB_pigeon'))
def npc(sp):
    t=nt; s=SPEC[sp]; nid=H(IDS['npc'][sp]); rid=H(IDS['race'][sp])
    t=re.sub(r'^FormKey: \S+',f'FormKey: {nid}:ITB_Wildlife.esp',t,count=1,flags=re.M)
    t=re.sub(r'(?m)^EditorID: .*$',f'EditorID: ITB_Bird{s["name"]}',t,count=1)
    t=re.sub(r'(?m)^ObjectBounds:\n  First: .*\n  Second: .*',f'ObjectBounds:\n  First: {s["obnd"][0]}\n  Second: {s["obnd"][1]}',t)
    t=re.sub(r'(?m)^Race: .*$',f'Race: {rid}:ITB_Wildlife.esp',t)
    t=re.sub(r'(?m)^AttackRace: .*$',f'AttackRace: {rid}:ITB_Wildlife.esp',t)
    t=re.sub(r'(?m)^WornArmor: .*\n','',t)
    t=re.sub(r'(?m)^Items:\n(?:(?:- |  ).*\n)+','',t)
    t=re.sub(r'(?m)^Packages:\n(?:- .*\n)+','',t)
    t=re.sub(r'(?m)^DefaultPackageList: .*\n','',t)
    t=re.sub(r'(?m)^(Name:\n  TargetLanguage: English\n  Value: ).*$',lambda m:m.group(1)+s['name'],t,count=1)
    t=re.sub(r'(?m)^Height: .*$',f'Height: {s["height"]}',t)
    assert 'WornArmor' not in t and rid in t
    return t
for sp in SPEC:
    wr(f'{Y}/Npcs/ITB_Bird{SPEC[sp]["name"]} - {H(IDS["npc"][sp])}_ITB_Wildlife.esp.yaml',npc(sp))
# --- sound descriptors: one call per species (the DLL plays them at the bird) and one wing flutter. Own records, own files (Birds/audio_build.py).
# Same category and mono 3D output model (SOMMono02000, audible to about 2000 units) as the ITB owl sound.
SOUNDS={'pigeon':(0x1660,4,'Pigeon call'),'dove':(0x1661,4,'Dove call'),'sparrow':(0x1662,4,'Sparrow call'),'robin':(0x1663,4,'Robin call'),
        'starling':(0x1664,4,'Starling call'),'crow':(0x1665,4,'Crow call'),'flap':(0x1666,3,'Wing flutter')}
def sound(key):
    fid,n,_=SOUNDS[key]
    edid='ITB_BirdWingFlutter' if key=='flap' else f'ITB_Bird{key.capitalize()}Call'
    files=''.join(f'- data\\sound\\fx\\itb\\birds\\{key}_{i}.wav\n' for i in range(1,n+1))
    t=(f'FormKey: {H(fid)}:ITB_Wildlife.esp\nEditorID: {edid}\nType: Standard\nCategory: 09F254:Skyrim.esm\nSoundFiles:\n{files}'
       f'OutputModel: 07F80C:Skyrim.esm\nPercentFrequencyVariance: 4\nPriority: 1\n')
    return edid,t
os.makedirs(f'{Y}/SoundDescriptors',exist_ok=True)
for key in SOUNDS:
    edid,t=sound(key)
    wr(f'{Y}/SoundDescriptors/{edid} - {H(SOUNDS[key][0])}_ITB_Wildlife.esp.yaml',t)
print('records written')

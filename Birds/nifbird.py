import struct,sys
def parse(d):
    p=d.index(b'\n')+1
    ver,endian,uver,nblocks,uver2=struct.unpack_from('<IBIII',d,p); p+=17
    for _ in range(3):
        n=d[p]; p+=1+n
    ntypes=struct.unpack_from('<H',d,p)[0]; p+=2; types=[]
    for _ in range(ntypes):
        n=struct.unpack_from('<I',d,p)[0]; p+=4; types.append(d[p:p+n].decode()); p+=n
    idx=list(struct.unpack_from('<%dH'%nblocks,d,p)); p+=2*nblocks
    sizes_off=p
    sizes=list(struct.unpack_from('<%dI'%nblocks,d,p)); p+=4*nblocks
    ns,mx=struct.unpack_from('<II',d,p); p+=8
    for _ in range(ns):
        n=struct.unpack_from('<I',d,p)[0]; p+=4+n
    ng=struct.unpack_from('<I',d,p)[0]; p+=4+4*ng
    off=[];o=p
    for s in sizes: off.append(o); o+=s
    return dict(types=types,idx=idx,sizes=sizes,sizes_off=sizes_off,data_start=p,off=off,end=o,nblocks=nblocks)
def sized(s): b=s.encode('latin1'); return struct.pack('<I',len(b))+b
def build(src,tex_path,sphere_radius=0.001):
    d=open(src,'rb').read(); h=parse(d); T=h['types']; I=h['idx']
    ts=[i for i,t in enumerate(I) if T[t]=='BSShaderTextureSet']; sp=[i for i,t in enumerate(I) if T[t]=='bhkSphereShape']
    assert len(ts)==1 and len(sp)==1
    blocks=[d[h['off'][i]:h['off'][i]+h['sizes'][i]] for i in range(h['nblocks'])]
    blocks[ts[0]]=struct.pack('<I',9)+sized(tex_path)+sized('')*8
    b=bytearray(blocks[sp[0]]); struct.pack_into('<f',b,4,sphere_radius); blocks[sp[0]]=bytes(b)
    sizes=[len(x) for x in blocks]
    head=bytearray(d[:h['data_start']]); struct.pack_into('<%dI'%h['nblocks'],head,h['sizes_off'],*sizes)
    return bytes(head)+b''.join(blocks)+d[h['end']:]
if __name__=='__main__':
    S='C:/Users/JulioV/AppData/Local/Temp/claude/27690b45ae6800bf/e8ba8216-5aca-40b6-8829-289afdb5596b/scratchpad/vb/smallbird01.nif'
    out='E:/WorkSpace/ITB/Birds/assets/meshes/itb/fauna/birds/'
    for sp in ('pigeon','dove','sparrow','crow','starling'):
        BS=chr(92); nb=build(S,BS.join(['textures','itb','fauna','birds','itb_bird_%s.dds'%sp]))
        open(out+'itb_bird_%s.nif'%sp,'wb').write(nb); print(sp,len(nb))

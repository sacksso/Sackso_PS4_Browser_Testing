import struct, subprocess, random, io, sys
from PIL import Image
def run(data):
    open('/tmp/t.ico','wb').write(data)
    subprocess.check_call(['/tmp/test_ico','/tmp/t.ico','/tmp/t.raw'])
    r=open('/tmp/t.raw','rb').read()
    nl=r.index(b'\n'); return r[:nl].decode(), r[nl+1:]
def make_bmp_ico(w,h,bpp,pix,mask,pal=None,topdown=False):
    # pix: list rows top->bottom of per-pixel values (tuples or palette idx)
    stride=((w*bpp+31)//32)*4
    xor=bytearray()
    rows = pix if topdown else pix[::-1]
    for row in rows:
        b=bytearray(stride)
        for x,v in enumerate(row):
            if bpp==32: b[x*4:x*4+4]=bytes((v[2],v[1],v[0],v[3]))
            elif bpp==24: b[x*3:x*3+3]=bytes((v[2],v[1],v[0]))
            elif bpp==8: b[x]=v
            elif bpp==4: b[x//2]|= v<<(0 if x&1 else 4)
            elif bpp==1: b[x//8]|= v<<(7-(x&7))
        xor+=b
    ms=((w+31)//32)*4
    andm=bytearray()
    mr = mask if topdown else mask[::-1]
    for row in mr:
        b=bytearray(ms)
        for x,v in enumerate(row):
            if v: b[x//8]|=1<<(7-(x&7))
        andm+=b
    palb=b''
    n=0
    if pal:
        n=len(pal); palb=b''.join(bytes((c[2],c[1],c[0],0)) for c in pal)
    hdr=struct.pack('<IiiHHIIiiII',40,w,(-2*h if topdown else 2*h),1,bpp,0,0,0,0,n if bpp<=8 else 0,0)
    data=hdr+palb+bytes(xor)+bytes(andm)
    d=struct.pack('<HHH',0,1,1)+struct.pack('<BBBBHHII',w%256,h%256,0,0,1,bpp,len(data),22)
    return d+data
random.seed(5); fails=0
def check(name,got,exp):
    global fails
    if got!=exp: fails+=1; print('FALLO',name)
    else: print('ok',name)
for (w,h) in [(16,16),(7,5),(32,20)]:
    # 32 bpp con alfa
    pix=[[(random.randrange(256),random.randrange(256),random.randrange(256),random.randrange(1,256)) for x in range(w)] for y in range(h)]
    mask=[[0]*w for _ in range(h)]
    for td in (False,True):
        k,raw=run(make_bmp_ico(w,h,32,pix,mask,topdown=td))
        exp=b''.join(bytes(p) for row in pix for p in row)
        check(f'32bpp {w}x{h} td={td}',(k,raw),(f'R {w} {h}',exp))
    # 32 bpp sin alfa + mascara
    pix2=[[(p[0],p[1],p[2],0) for p in row] for row in pix]
    mask=[[1 if (x+y)%3==0 else 0 for x in range(w)] for y in range(h)]
    k,raw=run(make_bmp_ico(w,h,32,pix2,mask))
    exp=b''.join(bytes((p[0],p[1],p[2],0 if mask[y][x] else 255)) for y,row in enumerate(pix2) for x,p in enumerate(row))
    check(f'32bpp sin alfa {w}x{h}',(k,raw),(f'R {w} {h}',exp))
    # 24 bpp
    pix3=[[tuple(random.randrange(256) for _ in range(3)) for x in range(w)] for y in range(h)]
    k,raw=run(make_bmp_ico(w,h,24,pix3,mask))
    exp=b''.join(bytes((p[0],p[1],p[2],0 if mask[y][x] else 255)) for y,row in enumerate(pix3) for x,p in enumerate(row))
    check(f'24bpp {w}x{h}',(k,raw),(f'R {w} {h}',exp))
    # 8, 4, 1 bpp
    for bpp in (8,4,1):
        npal=1<<bpp if bpp<8 else 40
        pal=[tuple(random.randrange(256) for _ in range(3)) for _ in range(npal)]
        idx=[[random.randrange(npal) for x in range(w)] for y in range(h)]
        k,raw=run(make_bmp_ico(w,h,bpp,idx,mask,pal))
        exp=b''.join(bytes((*pal[v],0 if mask[y][x] else 255)) for y,row in enumerate(idx) for x,v in enumerate(row))
        check(f'{bpp}bpp {w}x{h}',(k,raw),(f'R {w} {h}',exp))
# PNG dentro de ICO (Pillow) y varios tamanos
im=Image.new('RGBA',(64,64),(255,0,0,255))
bio=io.BytesIO(); im.save(bio,'ICO',sizes=[(16,16),(32,32),(64,64)]); k,raw=run(bio.getvalue())
check('png en ico (Pillow)',k,'P')
if k=='P': 
    from PIL import Image as I; print('  png',I.open(io.BytesIO(raw)).size)
# basura / truncados no deben romper (ASan)
good=make_bmp_ico(16,16,32,[[(1,2,3,4)]*16]*16,[[0]*16]*16)
for i in range(0,len(good),7):
    run(good[:i])
for i in range(300):
    b=bytearray(good); 
    for _ in range(4): b[random.randrange(len(b))]=random.randrange(256)
    run(bytes(b))
print('fuzz ok'); print('FALLOS',fails); sys.exit(1 if fails else 0)

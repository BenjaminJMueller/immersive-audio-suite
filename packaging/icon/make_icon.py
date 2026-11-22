# Draws the Raumklang app icon: python3 packaging/icon/make_icon.py packaging/icon/icon.png
# (8 speakers outside facing in, 4 inside facing out, a source in the middle).
import math, sys
from PIL import Image, ImageDraw
S=4; N=1024*S
img=Image.new("RGBA",(N,N),(0,0,0,0))
# squircle mask
def squircle(size, r):
    m=Image.new("L",(size,size),0); d=ImageDraw.Draw(m)
    d.rounded_rectangle([0,0,size-1,size-1],radius=r,fill=255); return m
B=824*S; off=(N-B)//2
bg=Image.new("RGBA",(B,B))
px=bg.load()
top=(34,37,46); bot=(11,12,16)
grad=Image.new("RGBA",(1,B))
for y in range(B):
    t=y/(B-1); grad.putpixel((0,y),tuple(int(top[i]*(1-t)+bot[i]*t) for i in range(3))+(255,))
bg=grad.resize((B,B))
img.paste(bg,(off,off),squircle(B,185*S))
d=ImageDraw.Draw(img)
# hairline border
d.rounded_rectangle([off,off,off+B-1,off+B-1],radius=185*S,outline=(255,255,255,28),width=2*S)
cx=cy=N/2
# source position and waves (own layer, so the alpha blends; clipped to the tile)
sx,sy=cx,cy
waves=Image.new("RGBA",(N,N),(0,0,0,0)); wd=ImageDraw.Draw(waves)
cols=[(10,132,255),(48,120,255),(88,100,240),(118,92,226),(140,88,214)]
for k,(rad,w,a) in enumerate([(58,12,240),(105,10,190),(175,8,140),(250,6,95),(330,5,60)]):
    r=rad*S
    wd.ellipse([sx-r,sy-r,sx+r,sy+r],outline=cols[k]+(a,),width=w*S)
clip=Image.new("L",(N,N),0); clip.paste(squircle(B,185*S),(off,off))
waves.putalpha(Image.composite(waves.getchannel("A"),Image.new("L",(N,N),0),clip))
img.alpha_composite(waves)
# speakers
def speaker(x,y,ang,scale,alpha):
    w,h=int(64*S*scale),int(46*S*scale)
    tile=Image.new("RGBA",(w+8,h+8),(0,0,0,0)); td=ImageDraw.Draw(tile)
    td.rounded_rectangle([4,4,4+w,4+h],radius=int(11*S*scale),fill=(232,235,242,alpha))
    # cone, on the side the speaker faces (+y in tile = towards facing)
    cr=int(12*S*scale); ccx=4+w/2; ccy=4+h*0.58
    td.ellipse([ccx-cr,ccy-cr,ccx+cr,ccy+cr],fill=(28,31,39,255))
    rot=tile.rotate(-math.degrees(ang),resample=Image.BICUBIC,expand=True)
    img.alpha_composite(rot,(int(x-rot.width/2),int(y-rot.height/2)))
for i in range(8):
    a=math.radians(i*45+22.5); R=322*S
    x,y=cx+R*math.cos(a),cy+R*math.sin(a)
    speaker(x,y,a+math.pi/2,1.0,240)   # faces the centre
for i in range(4):
    a=math.radians(i*90+45); R=150*S
    x,y=cx+R*math.cos(a),cy+R*math.sin(a)
    speaker(x,y,a-math.pi/2,0.78,215)  # faces outwards
# source dot
r=30*S
d.ellipse([sx-r,sy-r,sx+r,sy+r],fill=(255,255,255,255))
r=16*S
d.ellipse([sx-r,sy-r,sx+r,sy+r],fill=(10,132,255,255))
out=img.resize((1024,1024),Image.LANCZOS)
out.save(sys.argv[1])


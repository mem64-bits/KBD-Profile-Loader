# Usage: python3 tools/make_dualsense_atlas.py <input-overlay presets/dualsense/dualsense.png> assets/controller/dualsense-atlas.png
# Builds assets/controller/dualsense-atlas.png from input-overlay's CC0 "dualsense" preset sheet.
# Left half: line art (white, tintable). Right half: fills (pressed states and the body silhouette).
# Both at half the sheet's resolution. PlayStation logos are erased.
import sys
from PIL import Image
import numpy as np
from scipy import ndimage
src, out = sys.argv[1], sys.argv[2]
im=np.array(Image.open(src).convert('RGBA')).astype(np.int32)
H,W=im.shape[:2]
r,g,b,a=im[...,0],im[...,1],im[...,2],im[...,3]
cyan=(a>0)&(r<120)&(g>150)&(b>150)
for (x0,y0,x1,y1) in [(628,500,725,563),(1545,8,1650,125)]:
    a[y0:y1,x0:x1]=0; cyan[y0:y1,x0:x1]=False
# Line art keeps only light pixels (the stick caps have a dark grey fill the app draws itself).
lum=(r*299+g*587+b*114)//1000
light=np.clip((lum-70)*255//150,0,255)
line=np.zeros_like(im); line[...,0:3]=255; line[...,3]=np.where(cyan,0,a*light//255)
fill=np.zeros_like(im); fill[...,0:3]=255; fill[...,3]=np.where(cyan,a,0)
BW,BH=1344,788
wall=line[:BH,:BW,3]>60
lab,n=ndimage.label(~wall)
border=set(np.unique(np.concatenate([lab[0],lab[-1],lab[:,0],lab[:,-1]])))-{0}
inside=~np.isin(lab,list(border))
fill[:BH,:BW,3]=np.maximum(fill[:BH,:BW,3],np.where(inside,255,0))
size=((W+1)//2,H//2)
L=Image.fromarray(line.astype(np.uint8),'RGBA').resize(size,Image.LANCZOS)
F=Image.fromarray(fill.astype(np.uint8),'RGBA').resize(size,Image.LANCZOS)
atlas=Image.new('RGBA',(size[0]*2,size[1]),(0,0,0,0)); atlas.paste(L,(0,0)); atlas.paste(F,(size[0],0))
atlas.save(out,optimize=True); print(atlas.size)

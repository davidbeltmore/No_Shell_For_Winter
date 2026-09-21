"""Original monochrome slot pictograms, matching the existing ACF silhouette family."""
from pathlib import Path
from PIL import Image, ImageDraw

OUT = Path(__file__).parent / 'Assets'
OUT.mkdir(exist_ok=True)

def make(name, draw):
    image = Image.new('RGBA', (256, 256))
    draw(ImageDraw.Draw(image))
    image.save(OUT / ('T_Equipment_' + name + '.png'))

white = (238, 240, 242, 255)
def line(d, points, width=12):
    d.line(points, fill=white, width=width, joint='curve')

make('Feet', lambda d: [d.polygon([(x,35),(x+50,35),(x+45,148),(x+67,176),(x+70,202),(x-5,202),(x-12,183),(x,148)], fill=white) for x in (30,150)])
make('Necklace', lambda d: (line(d, [(55,30),(45,95),(73,145),(128,172),(183,145),(211,95),(201,30)], 10), d.polygon([(128,154),(153,190),(128,229),(103,190)], fill=white)))
make('UpperArms', lambda d: [d.polygon([(x,46),(x+51,27),(x+78,58),(x+70,108),(x+61,179),(x+13,195),(x,147)], fill=white) for x in (23,144)])
make('Forearms', lambda d: [d.polygon([(x,42),(x+66,57),(x+55,207),(x+5,216)], fill=white) for x in (27,159)])
make('Gloves', lambda d: [d.polygon([(x+13,219),(x+8,153),(x-2,99),(x+11,85),(x+30,118),(x+25,44),(x+36,36),(x+48,96),(x+44,24),(x+55,23),(x+66,100),(x+69,40),(x+80,42),(x+79,107),(x+86,70),(x+98,73),(x+91,146),(x+73,185),(x+71,219)], fill=white) for x in (15,141)])
make('Accessory', lambda d: (d.regular_polygon((128,126,96),8,rotation=22.5,outline=white,width=12),d.polygon([(128,55),(152,101),(199,127),(152,153),(128,199),(104,153),(57,127),(104,101)],fill=white)))
make('UnderwearTorso', lambda d: (line(d,[(42,34),(47,142)],10),line(d,[(214,34),(209,142)],10),d.pieslice((36,75,129,180),0,180,fill=white),d.pieslice((127,75,220,180),0,180,fill=white),line(d,[(36,133),(220,133)],12)))
make('UnderwearLegs', lambda d: (d.polygon([(28,65),(228,65),(181,130),(154,194),(102,194),(75,130)],fill=white),d.rectangle((28,48,228,60),fill=white)))
make('Satchel', lambda d: (d.arc((76,22,180,115),180,360,fill=white,width=12),d.rounded_rectangle((40,64,216,226),radius=24,outline=white,width=12),d.polygon([(44,82),(212,82),(195,134),(128,154),(61,134)],fill=white),d.rounded_rectangle((115,137,141,172),radius=5,outline=white,width=6)))
print('Generated 9 original slot pictograms.')

import math
import re

from shapely import affinity, make_valid
from shapely.geometry import LineString, Point, Polygon, box
from shapely.ops import unary_union


def gerber(text):
    apertures, objects, contours = {}, [], []
    pos, mode, region = (0.0, 0.0), '01', False
    for line in text.splitlines():
        m = re.fullmatch(r'%ADD(\d+)(C|R|O|Rect|RoundRect),([0-9.X-]+)\*%', line)
        if m:
            apertures[m[1]] = (m[2], [float(v) for v in m[3].split('X')])
            continue
        m = re.fullmatch(r'(?:G54)?D(\d+)\*', line)
        if m:
            aperture = apertures[m[1]]
            continue
        if line == 'G36*':
            region, contours = True, []
        elif line == 'G37*':
            for contour in contours:
                g = make_valid(Polygon(contour))
                objects.extend(q for q in getattr(g, 'geoms', [g]) if q.area > 0)
            region = False
        m = re.fullmatch(r'(?:G(0[123]))?(X-?\d+)?(Y-?\d+)?(I-?\d+)?(J-?\d+)?D(0[123])\*', line)
        if not m or not (m[2] or m[3]):
            continue
        mode = m[1] or mode
        dims = lambda v, d: int(v[1:]) / 100000 if v else d
        nxt = (dims(m[2], pos[0]), dims(m[3], pos[1]))
        if m[6] == '02':
            if region:
                contours.append([nxt])
        elif m[6] == '03':
            kind, vals = aperture
            x, y = nxt
            w, h = vals[0], vals[1] if len(vals) > 1 else vals[0]
            if kind == 'C':
                g = Point(nxt).buffer(w / 2, quad_segs=24)
            elif kind == 'O':
                off = abs(w-h) / 2
                g = (LineString([(x-off,y),(x+off,y)]) if w >= h else LineString([(x,y-off),(x,y+off)])).buffer(min(w,h)/2,quad_segs=24)
            elif kind == 'RoundRect':
                _, a, b, c, d = vals
                g = Polygon([(x+a,y+b),(x+c,y+d),(x-a,y-b),(x-c,y-d)]).buffer(w/2,quad_segs=24)
            else:
                g = box(x-w/2,y-h/2,x+w/2,y+h/2)
                if kind == 'Rect':
                    g = affinity.rotate(g, vals[2])
            objects.append(g)
        else:
            if mode == '01':
                seq = [pos, nxt]
            else:
                cx, cy = pos[0]+dims(m[4], 0), pos[1]+dims(m[5], 0)
                a, b = (math.atan2(y-cy,x-cx) for x,y in [pos,nxt])
                delta = (b-a)%(2*math.pi) if mode == '03' else -((a-b)%(2*math.pi))
                rad = math.hypot(pos[0]-cx,pos[1]-cy)
                n = max(2, math.ceil(abs(delta) / math.radians(2)))
                seq = [pos] + [(cx+rad*math.cos(a+delta*i/n),cy+rad*math.sin(a+delta*i/n)) for i in range(1,n)] + [nxt]
            if region:
                contours[-1].extend(seq[1:])
            else:
                assert aperture[0] == 'C', aperture
                objects.append(LineString(seq).buffer(aperture[1][0]/2, quad_segs=24))
        pos = nxt
    return unary_union(objects)

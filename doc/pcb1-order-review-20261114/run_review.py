import collections
import hashlib
import json
import math
import pathlib
import re
import zipfile

from shapely import make_valid
from shapely.geometry import Point, box
from shapely.strtree import STRtree

from gerber_geometry import gerber

ROOT = pathlib.Path(__file__).resolve().parent


def load(name):
    return json.loads((ROOT/name).read_text(encoding='utf8'))


def records(source):
    for row in source.splitlines():
        if '||' not in row:
            continue
        a, b = row.split('||', 1)
        b = b.rstrip('|')
        yield json.loads(a), json.loads(b) if b else None


def active(rows):
    current = {}
    for h,v in rows:
        if h['type']=='DOCHEAD':
            continue
        key = (h['type'], h.get('id',h.get('ticket')))
        if v is None:
            current.pop(key, None)
        else:
            current[key] = v
    return current


def drill_rows(drl):
    dia = {}
    current = None
    for ln in drl.splitlines():
        if m := re.fullmatch(r'(T\d+)C([\d.]+)', ln):
            dia[m[1]] = float(m[2])
        elif ln in dia:
            current = dia[ln]
        elif m := re.fullmatch(r'X([\d.-]+)Y([\d.-]+)', ln):
            yield (float(m[1]), float(m[2]), current)


def main():
    parts = lambda n: {c['props']['Designator']: c for c in load(n)['components'].values()}
    s, p = parts('P1-netlist.json'), parts('PCB1-netlist.json')
    nets = lambda c: {n: v['net'] for n,v in c['pinInfoMap'].items() if v['net']}
    padrows = load('PCB1-pads.json')
    padnet = {f'{ref}_{pn}': net for ref,rr in padrows.items() for _,pn,net,*_ in rr}
    padcounts = collections.Counter(f'{ref}_{pn}' for ref,rr in padrows.items() for _,pn,net,*_ in rr if net)
    assert not any(n>1 for n in padcounts.values()), 'A named pad needs explicit position disambiguation'
    net_errors = []
    for ref in sorted(s.keys() | p.keys()):
        if ref not in s or ref not in p:
            net_errors.append([ref, 'absent from schematic or PCB'])
        elif nets(s[ref]) != nets(p[ref]) or s[ref]['props'].get('Footprint')!=p[ref]['props'].get('Footprint'):
            net_errors.append([ref, nets(s[ref]), nets(p[ref])])
        elif nets(p[ref]) != {pn:n for _,pn,n,*_ in padrows.get(ref,[]) if n}:
            net_errors.append([ref, 'netlist / actual PCB pad mismatch'])
    src = list(records((ROOT/'PCB1.epcb').read_text(encoding='utf8')))
    sch = list(records((ROOT/'P1.esch').read_text(encoding='utf8')))
    end = list(records((ROOT/'PCB1-end.epcb').read_text(encoding='utf8')))
    src_stable = active(src)==active(end)
    archive_docs = {}
    with zipfile.ZipFile(ROOT/'BK7258-Dashboard.epro2') as z:
        assert z.testzip() is None
        rr = records(z.read(next(n for n in z.namelist() if n.endswith('.epru'))).decode('utf8'))
        doc=None
        docs=collections.defaultdict(list)
        for h,v in rr:
            if h['type']=='DOCHEAD':
                doc=v.get('uuid')
            elif doc:
                docs[doc].append((h,v))
        for local in [sch,src]:
            h,v=local[0]
            assert h['type']=='DOCHEAD'
            a,b = active(local),active(docs[v['uuid']])
            archive_docs[v['docType']] = {
                'local_objects':len(a),'archive_objects':len(b),
                'different_shared_objects_by_type':dict(collections.Counter(k[0] for k in a.keys()&b.keys() if a[k]!=b[k])),
                'local_only_by_type':dict(collections.Counter(k[0] for k in a.keys()-b.keys())),
                'archive_only_by_type':dict(collections.Counter(k[0] for k in b.keys()-a.keys())),
            }
    layer_files={1:'Gerber_TopLayer.GTL',15:'Gerber_InnerLayer1.G1',16:'Gerber_InnerLayer2.G2',2:'Gerber_BottomLayer.GBL'}
    with zipfile.ZipFile(ROOT/'PCB1-Gerber-inspection.zip') as z:
        assert z.testzip() is None
        all_copper={k:gerber(z.read(v).decode()) for k,v in layer_files.items()}
        pth=list(drill_rows(z.read('Drill_PTH_Through.DRL').decode()))
        npth=list(drill_rows(z.read('Drill_NPTH_Through.DRL').decode()))
        fly=json.loads(z.read('FlyingProbeTesting.json'))
        outline=z.read('Gerber_BoardOutlineLayer.GKO').decode()
        paste=gerber(z.read('Gerber_BottomPasteMaskLayer.GBP').decode())
        mask=gerber(z.read('Gerber_BottomSolderMaskLayer.GBS').decode())
    edges=re.findall(r'^G03X(\d+)Y(\d+)I(-?\d+)J0D01\*$',outline,re.M)
    assert len(edges)==2 and outline.count('D01*')==2
    (x1,y1,i1),(x2,y2,i2)=[tuple(int(v)/1e5 for v in r) for r in edges]
    assert y1==y2 and i1==-i2 and abs(x1-x2)==2*abs(i1)
    cx=(x1+x2)/2; cy=y1; radius=abs(i1)
    board=Point(cx,cy).buffer(radius,quad_segs=1024)
    copper={k:make_valid(g.intersection(board)) for k,g in all_copper.items()}
    islands={k:[x for x in getattr(g,'geoms',[g]) if x.area>0] for k,g in copper.items()}
    trees={k:STRtree(x) for k,x in islands.items()}
    parents={(k,i):(k,i) for k,v in islands.items() for i in range(len(v))}

    def hit(k,x,y):
        return [(k,int(i)) for i in trees[k].query(Point(x,y).buffer(.004),predicate='intersects')]

    def root(k):
        while parents[k]!=k:
            parents[k]=parents[parents[k]]
            k=parents[k]
        return k

    for x,y,_ in pth:
        linked=[a for k in layer_files for a in hit(k,x,y)]
        for a in linked[1:]:
            parents[root(a)]=root(linked[0])
    groups=collections.defaultdict(list)
    unhit=[]
    checked=[]
    for row in fly['pins']['rows']:
        i=dict(zip(fly['pins']['fields'],row))
        ref=i['PIN_NAME']
        layer={'T':1,'B':2}.get(i['LAYER'])
        net=padnet[ref]
        if not net or not layer or str(i['PIN_TYPE']).upper() not in ('SMD','PTH'):
            continue
        checked.append(ref)
        hits=hit(layer,i['PIN_X']*.0254,i['PIN_Y']*.0254)
        if len(hits)!=1:
            unhit.append([ref,net,hits])
        else:
            groups[root(hits[0])].append([net,ref])
    bynet=collections.defaultdict(list)
    shorts=[]
    for group in groups.values():
        g=collections.defaultdict(list)
        for net,ref in group:
            g[net].append(ref)
        if len(g)>1:
            shorts.append(dict(g))
        for net,refs in g.items():
            bynet[net].append(refs)
    antenna=next(v for h,v in src if h['type']=='REGION' and v['regionType']=='PROHIBIT')
    path=antenna['path'][0]
    coord=[x for x in path if isinstance(x,(int,float))]
    xx,yy=coord[0::2],coord[1::2]
    ant=box(min(xx)*.0254,min(yy)*.0254,max(xx)*.0254,max(yy)*.0254)
    holes=[dict(center=[x,y],diameter=dia,board_edge=round(radius-math.hypot(x-cx,y-cy)-dia/2,4),
                copper={k:round(Point(x,y).distance(v)-dia/2,4) for k,v in copper.items()}) for x,y,dia in npth]
    named_new_pad_openings={}
    for name in ['U8','U9','C26','C27','C28','C29']:
        named_new_pad_openings[name]={pn: {'paste':paste.covers(Point(x*.0254,y*.0254)), 'mask':mask.covers(Point(x*.0254,y*.0254))}
                                       for _,pn,_,x,y,layer in padrows[name]}
    result = dict(schematic_parts=len(s),pcb_parts=len(p),pads=sum(map(len,padrows.values())),
                  net_or_footprint_differences=net_errors,pcb_unchanged_during_export=src_stable,
                  native_archive_documents=archive_docs, strict_pcb_errors=len(load('PCB1-drc.json')),
                  schematic_rules=dict(collections.Counter(x['rule'] for x in load('P1-drc.json'))),
                  board_center=[cx,cy],board_diameter=2*radius,plated_drills=len(pth),
                  checked_named_pads=len(checked),named_nets=len(bynet),
                  named_pads_not_in_flying_probe=sorted(set(padcounts)-set(checked)),
                  shorts=shorts,named_nets_split={k:v for k,v in bynet.items() if len(v)>1},
                  pads_no_single_copper_island=unhit, antenna_copper_mm2={k:round(v.intersection(ant).area,6) for k,v in copper.items()},
                  outside_board_copper_mm2={k:round(v.difference(board.buffer(.002)).area,6) for k,v in all_copper.items()},
                  npth=holes,new_pad_openings=named_new_pad_openings,
                  gerber_sha256=hashlib.sha256((ROOT/'PCB1-Gerber-inspection.zip').read_bytes()).hexdigest())
    (ROOT/'review-results.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf8')
    print(json.dumps(result,indent=2,ensure_ascii=False))


if __name__=='__main__':
    main()

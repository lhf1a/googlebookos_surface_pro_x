#!/usr/bin/env python3
"""Learn a bounded, data-only tap adaptation proposal; never activates a driver rule."""
from pathlib import Path
import argparse,hashlib,json,math,statistics

def groups_from_events(events):
    groups=[];current=None
    for e in events:
        if e.get('kind')!='event' or not 0<=e.get('trial',-1)<15:continue
        pp=e.get('pointers',[])
        if not pp:continue
        p=pp[0];x,y=p['x'],p['y']
        if not all(math.isfinite(v) for v in (x,y)):raise ValueError('nonfinite coordinate')
        if e['action']==0:
            current={'trial':e['trial'],'start':e['time_ms'],'end':None,'first':[x,y],'last':[x,y],'peak':1,'cancelled':False}
            groups.append(current)
        if current is not None:
            current['last']=[x,y];current['peak']=max(current['peak'],len(pp))
            if e['action'] in (1,3):
                current['end']=e['time_ms'];current['cancelled']=e['action']==3;current=None
    return groups

def reconnect_allowed(gap_ms,distance,previous_peak,current_peak,raw_support,limit_ms,radius):
    # Output timestamps alone cannot distinguish rapid deliberate taps from a dropout.
    return (raw_support is True and previous_peak==current_peak==1 and
            0<=gap_ms<=limit_ms and math.isfinite(distance) and distance<=radius)

def plan(summary,events,operator_run,context_bytes=b''):
    if summary.get('run')!=operator_run:raise ValueError('operator confirmation belongs to another run')
    if not summary.get('complete') or summary.get('completed_trials')!=15 or summary.get('truncated'):raise ValueError('incomplete or truncated capture')
    device=summary['device']
    if len(device)!=64 or any(c not in '0123456789abcdef' for c in device):raise ValueError('device identity')
    if sum(summary['starts'])<15 or any(v<1 for v in summary['starts']):raise ValueError('not all targets observed')
    groups=groups_from_events(events)
    if any(g['end'] is None or g['cancelled'] for g in groups):raise ValueError('incomplete gesture')
    # Include only labelled single-finger same-target transitions during training.
    links=[]
    for a,b in zip(groups,groups[1:]):
        if a['trial']!=b['trial'] or max(a['peak'],b['peak'])!=1:continue
        gap=b['start']-a['end'];dist=math.dist(a['last'],b['first'])
        if 0<=gap<=80 and dist<=.04:links.append({'trial':b['trial']+1,'gap_ms':gap,'distance':dist})
    eligible=len(links)>=3 and len({x['trial'] for x in links})>=3
    limit=radius=0
    typical=[]
    if eligible:
        med=statistics.median(x['gap_ms'] for x in links)
        mad=statistics.median(abs(x['gap_ms']-med) for x in links)
        # Learn the dominant short-gap mode, not the worst outlier. Hard cap16ms.
        limit=int(min(16,max(8,math.ceil(med+3*mad+5))))
        typical=[x for x in links if x['gap_ms']<=limit]
        eligible=len(typical)>=3 and len({x['trial'] for x in typical})>=3
        if eligible:radius=min(.02,max(.002,max(x['distance'] for x in typical)+.001))
    supported=0;geometry_only=0
    if eligible:
        for x in links:
            geometry_only+=reconnect_allowed(x['gap_ms'],x['distance'],1,1,True,limit,radius)
            supported+=reconnect_allowed(x['gap_ms'],x['distance'],1,1,False,limit,radius)
    return {
      'schema':'SPX-TAP-PLAN-1','run':summary['run'],'device':device,
      'coordinate_profile_sha256':hashlib.sha256(context_bytes).hexdigest(),
      'observed':{'targets':15,'reported_gestures':len(groups),'fragmented_targets':sorted({g['trial']+1 for g in groups if sum(h['trial']==g['trial'] for h in groups)>1}),'max_simultaneous':max(summary['peak_simultaneous']),'within_target_links':links},
      'first_action':{'name':'MT_BTN_state_coherence','devices':['045e:0996','045e:0c21'],'rule':'BTN_TOUCH equals whether emitted valid MT slots exist; retain prior stable coordinates for an unstable still-valid slot; invalid/outside/disappeared contacts release immediately','onset_delay_ms':0,'release_delay_ms':0,'merge_genuine_fingers':False},
      'conditional_gap_candidate':{'eligible_from_labelled_data':eligible,'enabled':False,'max_gap_ms':limit if eligible else 0,'normalized_radius':round(radius,6) if eligible else 0,'requires':'After coherence fix, collect residual paired raw+MT evidence proving same blob survives gap, plus deliberate double-tap and pinch controls; explicit preview/confirmation only','hard_max_gap_ms':16,'real_release_must_be_immediate':True},
      'offline_output_only_screening':{'strict_accepted_merges':supported,'strict_output_gestures':len(groups)-supported,'geometry_only_candidate_links':geometry_only,'hypothetical_count_without_raw_gate':len(groups)-geometry_only,'warning':'Geometry-only count is NOT a validated repair and not deployed; reusing training labels is not an independent test.'},
      'other_cases':{'idle_sparse_or_bounded_band':'retain existing identity-bound noise interpolation; never learn from intentional taps','genuine_multifinger':'preserve independent slots; no global single-finger cap','broad_cross_edge_noise':'report unsupported; no expanding blind mask','large_coordinate_offset':'retain robust monotone coordinate fit and skip capability','insufficient_or_contradictory_data':'no new policy; retain prior settings'},
      'deployment':'proposal_only_no_live_gap_delay', 'raw_available_for_this_run':False}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    s=json.loads((a.capture/'point15-summary.json').read_text());events=[json.loads(l) for l in (a.capture/'point15-events.jsonl').read_text().splitlines()]
    confirmation=(a.capture/'point15-operator-confirmed.txt').read_text().splitlines()
    if len(confirmation)<2 or confirmation[1]!='SINGLE_FINGER_ONE_TAP_PER_TARGET':raise ValueError('operator condition not confirmed')
    report=plan(s,events,confirmation[0],(a.capture/'point15-context.profile').read_bytes())
    a.output.mkdir(parents=True,exist_ok=False)
    for name in ('plan.json','report.json'):(a.output/name).write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    (a.output/'sources.json').write_text(json.dumps({f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(a.capture.iterdir()) if f.is_file()},indent=2)+'\n')
    print(json.dumps(report,ensure_ascii=False,indent=2))
if __name__=='__main__':main()

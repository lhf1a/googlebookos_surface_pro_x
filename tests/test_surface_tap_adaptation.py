from pathlib import Path
import importlib.util,unittest
R=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('tap',R/'scripts/analyze-surface-taps.py');tap=importlib.util.module_from_spec(spec);spec.loader.exec_module(tap)
class Adaptive(unittest.TestCase):
 def data(self):
  s={'run':'run','device':'a'*64,'complete':True,'completed_trials':15,'truncated':False,'starts':[1]*15,'peak_simultaneous':[1]*15}
  events=[]
  for i in range(15):
   at=i*2000
   def event(t,a,x):return {'kind':'event','trial':i,'time_ms':t,'action':a,'pointers':[{'x':x,'y':.5}]}
   events.extend([event(at,0,.5),event(at+50,1,.5)])
   if i<4:s['starts'][i]+=1;events.extend([event(at+57,0,.501),event(at+100,1,.501)])
  return s,events
 def test_bound_proposal_and_unknown_raw_keeps_every_gesture(self):
  s,e=self.data();p=tap.plan(s,e,'run',b'profile')
  self.assertEqual(p['conditional_gap_candidate']['max_gap_ms'],12)
  self.assertFalse(p['conditional_gap_candidate']['enabled'])
  self.assertEqual(p['offline_output_only_screening']['strict_output_gestures'],19)
  self.assertEqual(p['offline_output_only_screening']['hypothetical_count_without_raw_gate'],15)
 def test_multifinger_doubletap_and_far_touch_not_merged(self):
  for gap,dist,prev,cur,support in [(7,.001,1,2,True),(7,.001,2,1,True),(40,.001,1,1,True),(7,.03,1,1,True),(7,.001,1,1,False)]:
   self.assertFalse(tap.reconnect_allowed(gap,dist,prev,cur,support,12,.004))
  self.assertTrue(tap.reconnect_allowed(7,.001,1,1,True,12,.004))
 def test_incomplete_wrong_confirmation_and_insufficient_evidence(self):
  s,e=self.data()
  with self.assertRaises(ValueError):tap.plan(s,e,'wrong')
  s['complete']=False
  with self.assertRaises(ValueError):tap.plan(s,e,'run')
  s,e=self.data();e=[x for x in e if x['time_ms']%2000 not in (57,100)];s['starts']=[1]*15
  self.assertFalse(tap.plan(s,e,'run')['conditional_gap_candidate']['eligible_from_labelled_data'])
if __name__=='__main__':unittest.main()

#!/usr/bin/env python3
"""Readiness observations: timeout starts at launch, never at READY."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'lib/toolchain'))
from toka_test_process import Supervisor
from test_toka_test_i2a import FIXTURE, assert_confirmed


def measure(output, name, delay_ms, budget_ms):
    directory=output/name;directory.mkdir(parents=True,exist_ok=False)
    events_path=directory/'fixture-events.jsonl'
    started=time.monotonic_ns();signals=[]
    class Observed(Supervisor):
        def send(self,pid,number):
            signals.append({'signal':number,'monotonic_ns':time.monotonic_ns()})
            super().send(pid,number)
    env=dict(os.environ,FIXTURE_EVENT_PATH=str(events_path),FIXTURE_STARTUP_DELAY_MS=str(delay_ms))
    raw=Observed().run([sys.executable,'-c',FIXTURE,'ignore'],directory,directory,'run',env,budget_ms)
    events=[json.loads(line) for line in events_path.read_text().splitlines()] if events_path.exists() else []
    entered=next((x['monotonic_ns'] for x in events if x['stage']=='entered'),None)
    ready=next((x['monotonic_ns'] for x in events if x['stage']=='ready'),None)
    ignored=next((x['monotonic_ns'] for x in events if x['stage']=='term_ignored'),None)
    term=next((x['monotonic_ns'] for x in signals if x['signal']==signal.SIGTERM),None)
    record={'name':name,'start_boundary':'harness immediately before Supervisor.run; deadline remains launch-based',
            'harness_start_ns':started,'configured_startup_delay_ms':delay_ms,'budget_ms':budget_ms,
            'entry_latency_ms':(entered-started)/1e6 if entered else None,
            'ready_latency_ms':(ready-started)/1e6 if ready else None,
            'ready_before_term':ready is not None and term is not None and ready<term,
            'handler_before_term':ignored is not None and term is not None and ignored<term,
            'events':events,'signal_requests':signals,'raw_phase':raw,'raw_stdout_bytes':(directory/'run.stdout').stat().st_size}
    (directory/'observation.json').write_text(json.dumps(record,indent=2)+'\n')
    assert_confirmed(raw);assert raw['trigger']=='timeout',record
    if delay_ms>budget_ms:
        assert not record['handler_before_term'] and not record['ready_before_term'],record
        assert raw['signal']==signal.SIGTERM and not raw['cleanup'].get('kill_sent',False),record
    else:
        # Missing readiness is a failed fixture precondition; no retries or deadline reset.
        assert record['handler_before_term'] and record['ready_before_term'],record
        assert raw['signal']==signal.SIGKILL and raw['cleanup']['kill_sent'],record
        assert (directory/'run.stdout').read_bytes()==b'READY\n',record
    return record


def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=False)
    records=[measure(a.output,'before-ready',2000,1000),
             measure(a.output,'after-ready-cold',250,5000),
             measure(a.output,'after-ready-warm',0,5000)]
    (a.output/'result.json').write_text(json.dumps({'result':'pass','records':records,
        'historical_first_failure':'preserved separately; initial phase logs unavailable; root cause unconfirmed',
        'production_deadlines_changed':False},indent=2)+'\n')

if __name__=='__main__':main()

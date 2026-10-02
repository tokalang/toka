#!/usr/bin/env python3
"""Conservative row-by-row evidence mapping; group counts never imply full coverage."""
import argparse,hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
# Only installed cases with the complete asserted row are marked covered.
CASES={
'D01':'discovery','D02':'discovery','D07':'literal-filter',
'D09':'allow-no-matches','D13':'project with space',
'D18':'nested','D20':'hidden','D21':'invalid-before-filter',
'D23':'help','D25a':'dash-entry','D25b':'literal-json-entry',
'R01':'discovery','R02':'compile-failure-continues','R05':'signal-then-ok','R12':'exit-130-is-test-failure'}
PARTIAL_CASES={
'D03':('explicit','Explicit selection/dedup tested; explicit auxiliary helper with failing default set not separately recorded.'),
'D06':('filter-or','OR demonstrated by two distinct matches; exclusion of a third c_test entry not in this installed input.'),
'D10':('no-tests','Missing tests directory covered; helper-only directory variant not separately recorded.'),
'D11':('allow-no-tests','Empty/no-directory form covered; helper-only variant not separately recorded.'),
'D14':('symlink-discovery','Symbolic file skipped; directory-link variant remains source-control scope.'),
'D24':('help-json','Help+JSON, duplicate JSON and duplicate allow cases all recorded; mapping this primary record conservatively.'),
'D04':('explicit','Two explicit entries are tested, but this record does not include an entry outside tests/.'),
'D05':('filter-nested','Exact literal selection tested with two candidates, not all three-row cardinality assertions.'),
'D08':('literal-filter','No-match behavior verified; separate nonexistent-name form covered by I2-B allow/filter fixtures.'),
'D12':('explicit','Relative duplicate deduplication; absolute/case aliases are covered by source controls, not this installed case.'),
'D15':('symlink-explicit','Rejects link and allow-empty with code 2; symbolic reason=invalid_entry is not asserted.'),
'D17':('subdirectory','Stable root/ID tested; resource/read-cwd assertion remains in the I1 fixture.'),
'D19':('nested-explicit','Rejects nested project; exact symbolic invalid_entry reason not asserted.'),
'D22':('missing-main','Missing main is tested; every invalid entry-signature variant is not.'),
'M02':('discovery','Combined phases validated by common C6 validator; per-row full boundary assertion additionally in source controls.'),
'M03':('compile-failure-continues','Compile failure not_started run tested; full R08/R13 variants are separate gaps.')}
C6={
'P04':('offline-cache-missing','Missing offline cache verified; all live network failure variants not exercised.'),
'P05':('lock-missing','Missing/malformed/stale lock cases present; all non-mutated lock bytes are not asserted per installed record.'),
'P06':('success','No-dependency/no-lock succeeds; identity fields available in complete report.'),
'P09':('dependency-source','Installed user/dependency provenance plus inert SDK/unknown controls; no real SDK warning emitter case.'),
'P10':('missing-python','Real Python/runner/helper failures; no exhaustive host-tool catalogue.'),
'J01':('live-interruption','Single JSON, live READY stderr and raw files; large output/ANSI bytes are source controls.'),
'J02':('runtime-timeout','Several actual JSON failure/interruption cases; R06 and R08 JSON combination not all installed.'),
'J05':('runtime-timeout','Timeout raw signal retained, but this normal TERM fixture does not exercise SIGKILL.'),
'J08':('configuration-error','Unknown argument before JSON, unknown total, errors; exact missing-source classification tested separately.'),
'T03':('live-interruption','Actual command PID interrupted through live stderr; only one active entry in C6 fixture.')}
A={
'R06':('compile-timeout','Compiler timeout and continuation from actual manager with controlled compiler.'),
'R07':('timeout-wait','Runtime timeout and continuation; formal report available in same run directory.'),
'R14':('compiler-crash','Compiler signal/no trigger returns 2 and stops next entry.'),
'T01':('timeout-children','Controlled child cleanup confirmed; original stdout/process record retained.'),
'T02':('timeout-ignore','TERM-to-KILL installed ignore fixture has readiness events; exact signal verified by dedicated ready harness.'),
'T04':('interrupt-context','Compile/probe/context/native variants are present; selection-stage interrupt not in installed cases.'),
'T07':('residual','Normal residue failure and cleanup; matching compiler-residue variant not installed.'),
'T10b':('timeout-wait','Actual CLI 1000 ms runtime deadline, raw trigger and cleanup.'),
'T11d':('interrupt-2','Repeated actual command PID SIGINT with bounded cleanup and not_run next item.'),
'T13':('interrupt-15','Actual command PID SIGTERM, cleanup, and next not_run.'),
'P12':('probe-timeout','Identity probe budget expiry returns infrastructure 2 and not_run.'),
'P14a':('lock-wait-cli-override','Effective 2000 ms lock wait verified; separate dependencies.lock_wait_ms report field not asserted.')}
SOURCE={
'D16':'test_symlinks_and_outside_and_nested_explicit','D27':'test_invalid_utf8_path_is_recoverable_configuration_error','P11':'test_launch_failure_preserves_not_run_and_native_errno',
'T05':'test_interrupt_cleanup_failure_overrides_130','T06':'test_cleanup_failure_stops_real_scheduler',
'T08':'test_reaped_identity_never_receives_a_signal','T10a':'test_options_bounds_and_duplicates',
'T11a':'test_confirmed_exit_precedes_deadline','T11b':'test_timeout_and_term_kill_escalation',
'T11c':'test_pending_signal_at_commit_boundary_is_included','A01':'test_concurrent_calls_independent_artifacts',
'A03':'test_raw_binary_ansi_and_unicode_logs_do_not_pollute_json','A06':'test_report_write_failure_changes_result_to_two',
'J04':'test_conflicting_nodes_missing_and_nested_paths_are_unknown','J06':'test_interrupt_cleanup_failure_overrides_130',
'J07':'test_artifact_creation_failure_still_delivers_memory_json','J10':'test_structured_warning_note_and_unknown_severity'}

def main():
    p=argparse.ArgumentParser();p.add_argument('--installed',type=Path,required=True);p.add_argument('--controls',type=Path,required=True)
    p.add_argument('--ready',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    document=ROOT/'docs/toka_test_v1_acceptance.md';rows=[]
    for line in document.read_text().splitlines():
        fields=[x.strip() for x in line.split('|')[1:-1]]
        if not fields or not re.fullmatch(r'[DPRTAJSMFVB]\d+[a-z]?',fields[0]):continue
        id=fields[0];record={'id':id,'contract':fields[1:],'coverage':'uncovered','evidence':[],
                             'gap':'No matching installed result for every input/expected assertion in this row.'}
        if id in CASES or id in PARTIAL_CASES:
            name=CASES.get(id) or PARTIAL_CASES[id][0];path=a.installed/'matrix-cases'/name/'result.json'
            data=json.loads(path.read_text());record.update(coverage='covered' if id in CASES else 'partial',
                evidence=[{'path':str(path),'actual_exit_code':data['exit_code'],'case':name}],
                gap=None if id in CASES else PARTIAL_CASES[id][1])
        elif id in C6 or id in A:
            name,note=(C6 if id in C6 else A)[id];folder='i2b-installed' if id in C6 else 'i2a-installed'
            path=a.installed/folder/name/'result.json';data=json.loads(path.read_text())
            record.update(coverage='partial',evidence=[{'path':str(path),'actual_exit_code':data['exit_code'],'case':name}],gap=note)
        elif id in SOURCE:
            method=SOURCE[id];matches=list(a.controls.glob('*-raw/'+method))
            record.update(coverage='partial' if matches else 'uncovered',
                          evidence=[{'path':str(path),'control':method,'scope':'source fixture; not an installed row completion'} for path in matches],
                          gap='Only related source control is mapped; do not infer every row assertion or installed behavior from it.')
        if id=='D02':record['fixture_note']='OTHER_TEST.tk is used instead of A_TEST.tk to avoid overwriting a_test.tk on case-insensitive hosts.'
        if id=='P01':record['gap']='Controlled registry/local SDK regressions exist; original Unicode 0.1.1 failure path remains unresolved and is not replaced.'
        if id in ('P02','P03','P07','P08','A04'):
            path=a.installed/'i1-installed/installed-result.json';record.update(coverage='partial',evidence=[{'path':str(path),'scope':'aggregate I1 installed assertions'}],gap='Related I1 installed checks pass; not all exact row input/report assertions are separately recorded.')
        if id=='M01':record.update(coverage='not_applicable',gap='Fixed compiler executes one combined compile/link call; no separate actual boundary exists.')
        if id=='T12':record.update(coverage='uncovered',gap='Windows managed backend is unsupported and outside core SDK targets; POSIX supervisor-start failure not injected here.')
        if id[0] in 'SFV' or id in ('B01','B02','B03'):
            record.update(coverage='outside_i2c',evidence=[],gap='#38/#39/platform-policy/B0/external-project tasks are independent gates; not inferred from I2-C.')
        rows.append(record)
    assert len(rows)==120 and len({r['id'] for r in rows})==120
    counts={state:sum(r['coverage']==state for r in rows) for state in ('covered','partial','uncovered','not_applicable','outside_i2c')}
    a.output.write_text(json.dumps({'schema':'toka.i2c-acceptance-map','version':1,'contract_sha256':hashlib.sha256(document.read_bytes()).hexdigest(),
        'rows':rows,'coverage_counts':counts,'full_matrix_pass':False,'preview_removal_authorized':False,
        'readiness_evidence':str(a.ready/'result.json'),'note':'Archive/install regressions passing do not close partial/uncovered acceptance rows.'},indent=2)+'\n')

if __name__=='__main__':main()

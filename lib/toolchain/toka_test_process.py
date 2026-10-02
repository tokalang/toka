"""POSIX test supervision. Never reap the group leader before the last signal."""
from contextlib import contextmanager
import ctypes
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

TERMINATE_GRACE_MS = 2000
KILL_WAIT_MS = 5000


class Interrupted(RuntimeError):
    pass


class SupervisionError(RuntimeError):
    pass


def group_members(pgid):
    """Read membership only, while our unreaped leader still anchors PGID.

    These IDs are never cancellation targets: all signals use the anchored group.
    Include zombies; inability to confirm their disappearance is an error.
    """
    if sys.platform == 'darwin':
        library = ctypes.CDLL('/usr/lib/libproc.dylib', use_errno=True)
        function = library.proc_listpids
        function.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p, ctypes.c_int]
        function.restype = ctypes.c_int
        size = 64
        while True:
            buffer = (ctypes.c_int * size)()
            used = function(2, pgid, buffer, ctypes.sizeof(buffer))  # PROC_PGRP_ONLY
            if used < 0:
                raise OSError(ctypes.get_errno(), 'proc_listpids failed')
            if used < ctypes.sizeof(buffer):
                return {pid for pid in buffer[:used // ctypes.sizeof(ctypes.c_int)] if pid > 0}
            size *= 2
    if sys.platform.startswith('linux'):
        members = set()
        for item in Path('/proc').iterdir():
            if not item.name.isdecimal():
                continue
            try:
                fields = (item / 'stat').read_text().rsplit(')', 1)[1].split()
                if int(fields[2]) == pgid:
                    members.add(int(item.name))
            except (FileNotFoundError, ProcessLookupError):
                continue
        return members
    raise SupervisionError('unsupported process group membership backend')


class OutputTail:
    """Regular-file capture avoids pipe EOF hangs from descendants; relay is live."""
    def __init__(self, paths):
        self.paths=paths;self.offsets={str(path):0 for path in paths};self.failed=False
    def pending_bytes(self):
        return sum(max(0,path.stat().st_size-self.offsets[str(path)]) for path in self.paths)
    def pump(self):
        if self.failed:return 0
        sent=0
        try:
            for path in self.paths:
                with path.open('rb') as stream:
                    stream.seek(self.offsets[str(path)])
                    for _ in range(4):
                        data=stream.read(65536)
                        if not data:break
                        target=getattr(sys.stderr,'buffer',None)
                        if target is not None:
                            import fcntl
                            flags=fcntl.fcntl(target.fileno(),fcntl.F_GETFL)
                            try:
                                fcntl.fcntl(target.fileno(),fcntl.F_SETFL,flags|os.O_NONBLOCK)
                                try:written=os.write(target.fileno(),data)
                                except BlockingIOError:written=0
                            finally:fcntl.fcntl(target.fileno(),fcntl.F_SETFL,flags)
                        else:
                            sys.stderr.write(data.decode('utf-8',errors='replace'));sys.stderr.flush()
                            written=len(data)
                        # File offsets advance only for bytes actually forwarded. Retry
                        # transient backpressure on the next poll without stalling deadlines.
                        self.offsets[str(path)]+=written;sent+=written
                        if written<len(data):return sent
            return sent
        except (OSError,ValueError):
            self.failed=True
            raise



class Supervisor:
    def __init__(self):
        self.interrupt_signal = None
        self.interrupt_count = 0

    def _interrupt(self, number, frame):
        if self.interrupt_signal is None:
            self.interrupt_signal = number
        self.interrupt_count += 1

    @contextmanager
    def signals(self):
        previous = {number: signal.signal(number, self._interrupt)
                    for number in (signal.SIGINT, signal.SIGTERM)}
        try:
            yield self
        finally:
            for number, handler in previous.items():
                signal.signal(number, handler)

    def check_interrupt(self):
        if self.interrupt_signal is not None:
            raise Interrupted('user interrupt')

    def exited(self, pid):
        # WNOWAIT preserves the leader identity, including after normal exit.
        return os.waitid(os.P_PID, pid, os.WEXITED | os.WNOHANG | os.WNOWAIT) is not None

    def members(self, pid):
        return group_members(pid)

    def send(self, pid, number):
        # Verify the wait right before every signal. Identity failure forbids cancellation.
        self.exited(pid)
        try:
            os.killpg(pid, number)
        except ProcessLookupError:
            pass

    def gone(self, pid):
        try:
            os.killpg(pid, 0)
            return False
        except ProcessLookupError:
            return True

    def run(self, command, root, directory, name, environment, timeout_ms):
        self.check_interrupt()
        stdout_path = directory / (name + '.stdout')
        stderr_path = directory / (name + '.stderr')
        started = time.monotonic()
        result = {'command': [str(x) for x in command], 'exit_code': None, 'signal': None,
                  'stdout': str(stdout_path), 'stderr': str(stderr_path),
                  'budget_ms': timeout_ms, 'backend': 'posix_process_group',
                  'scope': 'direct_child_and_process_group', 'trigger': None,
                  'identity': 'unreaped_leader_wnowait', 'requested_signals': [],
                  'cleanup': {'status': 'not_needed', 'direct_child_reaped': False,
                              'group_gone': False, 'logs_closed': False}}
        child = None
        relay = None
        cleanup_started = None
        cleanup = result['cleanup']
        try:
            with stdout_path.open('xb') as stdout, stderr_path.open('xb') as stderr:
                self.check_interrupt()
                child = subprocess.Popen(command, cwd=root, env=environment,
                                         stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr,
                                         start_new_session=True)
                result['pid'] = result['pgid'] = child.pid
                relay=OutputTail([stdout_path,stderr_path])
                try:
                    while True:
                        relay.pump()
                        ended = self.exited(child.pid)
                        if self.interrupt_signal is not None:
                            result['trigger'] = 'interrupt'
                            break
                        if ended:
                            if self.members(child.pid) - {child.pid}:
                                result['trigger'] = 'residual_process'
                            break
                        if (time.monotonic() - started) * 1000 >= timeout_ms:
                            result['trigger'] = 'timeout'
                            break
                        time.sleep(0.01)
                except (OSError, SupervisionError) as error:
                    result['supervision_error'] = str(error)
                    result['os_error'] = getattr(error,'errno',None)
                    result['trigger'] = 'supervision_error'
                result['execution_duration_ms']=(time.monotonic()-started)*1000
                cleanup_started=time.monotonic()
                # All signals precede waitpid: a zombie leader is still our anchor.
                if result['trigger'] is not None:
                    cleanup['status'] = 'pending'
                    try:
                        self.send(child.pid, signal.SIGTERM)
                        result['requested_signals'].append(signal.SIGTERM)
                        until = time.monotonic() + TERMINATE_GRACE_MS / 1000
                        while time.monotonic() < until:
                            if self.exited(child.pid) and not (self.members(child.pid) - {child.pid}):
                                break
                            relay.pump()
                            time.sleep(0.01)
                        else:
                            self.send(child.pid, signal.SIGKILL)
                            result['requested_signals'].append(signal.SIGKILL)
                            cleanup['kill_sent'] = True
                    except (OSError, SupervisionError) as error:
                        cleanup['error'] = str(error)
                        # Preserve identity; one final bounded kill attempt, never bare IDs.
                        try:
                            self.send(child.pid, signal.SIGKILL)
                            result['requested_signals'].append(signal.SIGKILL)
                            cleanup['kill_sent'] = True
                        except (OSError, SupervisionError) as kill_error:
                            cleanup['kill_error'] = str(kill_error)
                until = time.monotonic() + KILL_WAIT_MS / 1000
                while not self.exited(child.pid) and time.monotonic() < until:
                    time.sleep(0.01)
                if self.exited(child.pid):
                    pid, status = os.waitpid(child.pid, os.WNOHANG)
                    if pid != child.pid:
                        raise SupervisionError('direct child wait did not confirm exit')
                    child.returncode = os.waitstatus_to_exitcode(status)
                    cleanup['direct_child_reaped'] = True
                    result['exit_code'] = child.returncode if child.returncode >= 0 else None
                    result['signal'] = -child.returncode if child.returncode < 0 else None
                    # No more signals after this point, even if confirmation fails.
                    while not self.gone(child.pid) and time.monotonic() < until:
                        time.sleep(0.01)
                    cleanup['group_gone'] = self.gone(child.pid)
                if not cleanup['direct_child_reaped'] or not cleanup['group_gone']:
                    cleanup['error'] = cleanup.get('error', 'bounded exit confirmation failed')
                while relay.pending_bytes() and not relay.failed and time.monotonic()<until:
                    if not relay.pump():time.sleep(.01)
                result['live_output']={'status':'complete' if not relay.pending_bytes() and not relay.failed else 'unavailable',
                                       'unforwarded_bytes':relay.pending_bytes()}
                if relay.pending_bytes() and not relay.failed:
                    raise BlockingIOError(11,'live stderr did not drain within bounded output confirmation')
            cleanup['logs_closed'] = True
        except (OSError, SupervisionError) as error:
            if child is not None and stdout.closed and stderr.closed:cleanup['logs_closed']=True
            cleanup['error'] = str(error)
            result['supervision_error'] = str(error)
            result['os_error'] = getattr(error,'errno',None)
        cleanup['duration_ms']=(time.monotonic()-cleanup_started)*1000 if cleanup_started is not None else None
        if child is None:
            result['launch_error'] = cleanup.get('error', 'child not started')
            cleanup['status'] = 'not_needed'
        elif cleanup.get('error') or not all(cleanup[k] for k in ('direct_child_reaped', 'group_gone', 'logs_closed')):
            cleanup['status'] = 'failed'
        else:
            cleanup['status'] = 'confirmed'
        result['duration_ms'] = (time.monotonic() - started) * 1000
        result['interrupt_signal'] = self.interrupt_signal
        result['interrupt_count'] = self.interrupt_count
        return result


def streamed_run(command, **options):
    """Native worker tools stay in its anchored group; retain and relay their pipes."""
    import selectors
    check=options.pop('check',False)
    text=options.pop('text',False)
    if options.get('stdout')!=subprocess.PIPE or options.get('stderr')!=subprocess.PIPE:
        return subprocess.run(command,check=check,text=text,**options)
    child=subprocess.Popen(command,**options)
    buffers={'stdout':bytearray(),'stderr':bytearray()}
    with selectors.DefaultSelector() as selector:
        selector.register(child.stdout,selectors.EVENT_READ,'stdout')
        selector.register(child.stderr,selectors.EVENT_READ,'stderr')
        while selector.get_map():
            for key,event in selector.select(.05):
                data=os.read(key.fileobj.fileno(),65536)
                if not data:
                    selector.unregister(key.fileobj);key.fileobj.close();continue
                buffers[key.data].extend(data)
                stream=getattr(sys.stdout if key.data=='stdout' else sys.stderr,'buffer',None)
                if stream is not None:stream.write(data);stream.flush()
                # OS fd 2 remains the worker's outer capture file despite Python redirects.
                os.write(2,data)
    code=child.wait()
    stdout,stderr=(bytes(buffers[name]) for name in ('stdout','stderr'))
    if text:stdout,stderr=stdout.decode('utf-8',errors='replace'),stderr.decode('utf-8',errors='replace')
    result=subprocess.CompletedProcess(command,code,stdout,stderr)
    if check and code:raise subprocess.CalledProcessError(code,command,stdout,stderr)
    return result

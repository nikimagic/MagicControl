"""Spawn only the supplied offline fixture, in an explicit scratch directory."""
from pathlib import Path
import json, subprocess, sys, tempfile, time

exe=Path(sys.argv[1]).resolve()
scratch=Path(sys.argv[2]).resolve()
scratch.mkdir(parents=True,exist_ok=True)
root=Path(tempfile.mkdtemp(prefix='процессы-',dir=scratch))
children=[]
def spawn(mode,label):
    p=subprocess.Popen([str(exe),mode,str(root),label],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    children.append(p)
    return p
def finish(p,expected=0):
    out,err=p.communicate('release\n',timeout=25)
    assert p.returncode==expected,(p.args,p.returncode,out,err)
    return out,err
def ready(label,p):
    path=root/'control'/f'{label}.ready'
    deadline=time.monotonic()+8
    while not path.exists():
        assert p.poll() is None,(p.args,p.communicate())
        assert time.monotonic()<deadline,('ready timeout',p.args)
        time.sleep(.01)
    return path.read_text()
def snapshots():
    return {p.name:p.read_bytes() for p in (root/'logs').glob('*.jsonl')}
try:
    # Hold three real OS leases. Make the active first slot look oldest.
    a=spawn('log-hold','A'); slot=ready('A',a)
    assert slot=='MagicControl-1.jsonl',slot
    b=spawn('log-hold','B'); assert ready('B',b)=='MagicControl-2.jsonl'
    c=spawn('log-hold','C'); assert ready('C',c)=='MagicControl-3.jsonl'
    import os
    for name in ('MagicControl-1.jsonl','battery-1.jsonl'): os.utime(root/'logs'/name,(1,1))
    before=snapshots(); assert len(before)==6
    _,err=finish(spawn('log-once','D'),4)
    assert 'All three log pairs are active' in err
    assert snapshots()==before,'active log pair truncated'
    finish(b)
    assert ready('A',a)=='MagicControl-1.jsonl'
    d=spawn('log-hold','D2'); assert ready('D2',d)=='MagicControl-2.jsonl'
    assert (root/'logs'/'MagicControl-1.jsonl').read_bytes()==before['MagicControl-1.jsonl']
    # Simulate fixture crash; OS must release the slot without deleting lock files.
    d.terminate(); d.communicate(timeout=8)
    e=spawn('log-once','E');finish(e)
    assert ready('E',e)=='MagicControl-2.jsonl'
    finish(a);finish(c)
    for name,data in snapshots().items():
        rows=[json.loads(row) for row in data.decode().splitlines()]
        assert len({row['session'] for row in rows})==1,name
        partner=('battery-' if name.startswith('MagicControl-') else 'MagicControl-')+name.split('-')[-1]
        assert rows[0]['session']==json.loads(snapshots()[partner].decode().splitlines()[0])['session']
    assert not list((root/'logs').glob('*.part*'))
    assert len(list((root/'logs').glob('*.jsonl')))==6

    # Concurrent actual cache readers/writers, with no hardware collectors.
    finish(spawn('inventory-write','Seed'))
    writers=[spawn('inventory-write',label) for label in ('W1','W2','W3')]
    readers=[spawn('inventory-read',label) for label in ('R1','R2')]
    for p in writers+readers:finish(p)
    cache=root/'logs_Fixture'/'bios-inventory.json'
    json.loads(cache.read_text())
    old=cache.read_bytes()
    finish(spawn('inventory-oversize','TooLarge'))
    assert cache.read_bytes()==old,'rejected oversized snapshot overwrote valid cache'
    holder=spawn('inventory-hold','Hold');ready('Hold',holder)
    waiting=spawn('inventory-write','After')
    time.sleep(.15)
    assert waiting.poll() is None and cache.read_bytes()==old
    holder.terminate();holder.communicate(timeout=8)
    finish(waiting)
    assert json.loads(cache.read_text())['inventory']['readAt'].startswith('After:')

    # The common atomic writer is independently safe with multiple publishers.
    orphan=root/'atomic.json.tmp';orphan.write_text('unowned old temporary')
    writers=[spawn('atomic-write',label) for label in ('X','Y','Z')]
    for p in writers:finish(p)
    data=json.loads((root/'atomic.json').read_text())
    assert set(data['data'])=={data['owner'][0]}
    finish(spawn('atomic-failure','Fail'))
    assert orphan.read_text()=='unowned old temporary'
    assert not list(root.glob('*.tmp-*'))
    assert not list((root/'logs_Fixture').glob('*.tmp-*'))
    print('PASS: three paired log leases, active protection, process exit recovery, serialized complete inventory, unique atomic publication/failure cleanup')
finally:
    for p in children:
        if p.poll() is None:
            p.terminate()
            p.communicate(timeout=8)

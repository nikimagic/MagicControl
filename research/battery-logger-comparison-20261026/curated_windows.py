"""Preserve selected byte-linked proof windows. No EC/firmware execution."""
from pathlib import Path
import hashlib,json,sys
OUT=Path(__file__).resolve().parent;ROOT=OUT.parents[1]
sys.path.insert(0,str(ROOT/'build/honor-analysis'))
from disasm51 import addresses,codeanalyzer,instructions
census=json.loads((OUT/'census.json').read_text(encoding='utf8'));rows=[]
specs={
'WUJIE':[(2,0xc574,0xc7c9,'healthy_voltage_and_AP_presence'),(2,0xd666,0xd675,'AP_presence_helper'),(0,0x6f7d,0x6f8f,'unsigned16_multiply'),(1,0xc088,0xc099,'voltage_to_charger_buffer'),(1,0xb1f9,0xb204,'voltage_pending_flag'),(0,0xd5bd,0xd66a,'charger_dispatch_commands15_and14'),(2,0xcffd,0xd0a1,'percentage_gate_SOC_Reached'),(1,0xbd14,0xbde1,'primary_secondary_charge_path_selection'),(1,0xb26c,0xb283,'zero_current_on_charge_path_transition'),(0,0xecb6,0xecd4,'startup_project_and742_capability'),(0,0xa4d3,0xa56e,'input_source_status7A5'),(2,0x8ae3,0x8b73,'thermal7A5_bit3'),(2,0x837f,0x83ce,'thermal_event_latch7C8_bit4'),(2,0xd9a3,0xd9ae,'thermal_raw44F_threshold70'),(2,0xa3f6,0xa422,'TypeC_TCSA_mirror984_bit7'),(1,0xa926,0xa972,'temperature_default_and_publication'),(1,0xae43,0xaee2,'alternative_current050E_to4A4'),(1,0xddbe,0xdde2,'base_words_to520_and522'),(2,0x9774,0x97a7,'native_table_to7D8_7D9_7DA'),(2,0x9bc6,0x9bf3,'native_table_to7A7_7A8_7A9'),(2,0x8d66,0x8dac,'RamFan_table_gate7C6_bit2'),(2,0x98bb,0x98d2,'host_override786'),(2,0x8eee,0x8f0a,'host_override787')],
'GM6_1.11':[(2,0xc110,0xc365,'healthy_voltage_v111'),(2,0xced7,0xcf68,'percentage_v111_fragment'),(1,0xde0f,0xde2a,'voltage_publication_v111_fragment')],
'GM6_1.12':[(2,0xc118,0xc36d,'healthy_voltage_v112'),(1,0xae23,0xae6e,'unsigned_current_XST1_publication'),(1,0xd347,0xd3e1,'current_sampler_v112_fragment'),(1,0xde0f,0xde2a,'voltage_publication_v112_fragment'),(0,0xc935,0xc94c,'voltage_command15_v112_fragment'),(2,0xceef,0xcf70,'percentage_v112_fragment')]}
for image in census['firmware']:
 raw=Path(image['path']).read_bytes();assert hashlib.sha256(raw).hexdigest()==image['sha256']
 for bank,start,end,name in specs[image['label']]:
  rom=raw[:0x8000]+raw[(bank+1)*0x8000:(bank+2)*0x8000]
  decode=codeanalyzer.CodeAnalyzer(instructions.Instructions(),rom,addresses.Addresses(),0,True,[],set())._CodeAnalyzer__disassemble_instruction
  pc=start;window=[]
  while pc<end:
   text,n=decode(pc);window.append(dict(CODE=pc,file_offset=pc if pc<0x8000 else bank*0x8000+pc,hex=rom[pc:pc+n].hex(),text=text.strip()));pc+=n
  rows.append(dict(image=image['label'],sha256=image['sha256'],name=name,bank=bank,start=start,end_requested=end,end_decoded=pc,aligned_end=pc==end,instructions=window,scope='Selected direct byte window. Fragments and calls are not a full CFG or hardware execution proof.'))
(OUT/'curated-windows.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf8')
(OUT/'CURATED-WINDOWS.asm').write_text('\n\n'.join(f"{w['image']} bank{w['bank']} {w['name']}\n"+'\n'.join(f"{r['CODE']:04X} {r['hex']:<10} {r['text']}"for r in w['instructions'])for w in rows)+'\n',encoding='utf8')
print('selected windows',len(rows),'non-aligned requested endpoints',[(w['image'],w['name'],hex(w['end_requested']),hex(w['end_decoded']))for w in rows if not w['aligned_end']])

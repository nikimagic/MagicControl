// Offline 8051 instruction decoding of saved bytes only; no emulation or hardware.
import ghidra.app.script.GhidraScript;
import ghidra.app.util.PseudoDisassembler;
import java.io.PrintWriter;
public class Gm6EcRawRanges extends GhidraScript {
 public void run() throws Exception {
  String[] a=getScriptArgs(); PseudoDisassembler decoder=new PseudoDisassembler(currentProgram);
  try(PrintWriter out=new PrintWriter(a[0],"UTF-8")){
   for(String range:a[1].split(",")){
    String[] ends=range.split(":");long at=Long.parseLong(ends[0],16),end=Long.parseLong(ends[1],16);
    out.println("\n// LINEAR RANGE "+range+"; boundaries need caller/branch validation");
    while(at<end){
     var ins=decoder.disassemble(toAddr(at));
     if(ins==null){out.printf("%04X UNDECODED\n",at);at++;continue;}
     StringBuilder bytes=new StringBuilder();for(byte b:ins.getBytes())bytes.append(String.format("%02X ",b&255));
     out.printf("%04X %-10s %s\n",at,bytes.toString(),ins.toString());
     at+=ins.getLength();
    }
   }
  }
 }
}

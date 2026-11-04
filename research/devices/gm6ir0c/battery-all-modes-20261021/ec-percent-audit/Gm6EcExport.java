// Static disassembly/decompilation of a fixed bank snapshot; never run firmware.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
import ghidra.program.model.address.Address;
import java.io.PrintWriter;
public class Gm6EcExport extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();String[] entries=args[1].split(",");
  for(String item:entries){Address a=toAddr(Long.parseLong(item,16));disassemble(a);if(getFunctionAt(a)==null)createFunction(a,null);}
  DecompInterface d=new DecompInterface();d.openProgram(currentProgram);
  try(PrintWriter out=new PrintWriter(args[0],"UTF-8")){
   for(String item:entries){Function f=getFunctionAt(toAddr(Long.parseLong(item,16)));out.println("\n// ENTRY "+item);
    if(f==null){out.println("NO FUNCTION");continue;}
    var result=d.decompileFunction(f,30,monitor);
    if(result.decompileCompleted()&&result.getDecompiledFunction()!=null)out.println(result.getDecompiledFunction().getC());
    else out.println("DECOMPILE ERROR: "+result.getErrorMessage());
    out.println("/* RAW INSTRUCTIONS");var ins=currentProgram.getListing().getInstructions(f.getBody(),true);int n=0;
    while(ins.hasNext()&&n++<1200){var i=ins.next();out.println(i.getAddress()+" "+i.toString());}out.println("*/");
   }
  }finally{d.dispose();}
 }
}

// Offline bank2 snapshot only. Never executes EC firmware.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
import ghidra.program.model.address.Address;
import java.io.PrintWriter;
public class DecompileGm6EcBank extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  long[] entries={0xC005,0xC110,0xC3E8,0xCE2D,0xCE52,0xCEA1,0xCEC0,0xCF71,0xCF90,0xCFDA,0xD6A5,0xD6C3,0xD70E,0xD81F,0xD86D,0xDC1E,0xDC2C,0xDE7A,0xD7B4,0xDCE5};
  for(long value:entries){Address a=toAddr(value);disassemble(a);if(getFunctionAt(a)==null)createFunction(a,null);}
  DecompInterface d=new DecompInterface();d.openProgram(currentProgram);
  try(PrintWriter out=new PrintWriter(args[0],"UTF-8")){
   for(long value:entries){Function f=getFunctionAt(toAddr(value));out.printf("\n// BANK2 %04X\n",value);
    if(f==null){out.println("NO FUNCTION");continue;}
    var result=d.decompileFunction(f,30,monitor);
    if(result.decompileCompleted()&&result.getDecompiledFunction()!=null)out.println(result.getDecompiledFunction().getC());
    else out.println("DECOMPILE ERROR: "+result.getErrorMessage());
   }
  }finally{d.dispose();}
 }
}

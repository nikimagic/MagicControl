import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import java.nio.file.*;
public class ExportManualCandidates extends GhidraScript {
 public void run() throws Exception {
  Path out=Paths.get(getScriptArgs()[0]);Files.createDirectories(out);
  var dec=new DecompInterface();dec.openProgram(currentProgram);
  var functions=currentProgram.getFunctionManager().getFunctions(true);int count=0;
  try{while(functions.hasNext()){
   var f=functions.next();var result=dec.decompileFunction(f,20,monitor);
   Files.writeString(out.resolve(f.getEntryPoint()+".c"),result.decompileCompleted()?result.getDecompiledFunction().getC():result.getErrorMessage());
   var body=new StringBuilder();var ins=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(ins.hasNext()){var i=ins.next();body.append(i.getAddress()+" "+i+"\n");}
   Files.writeString(out.resolve(f.getEntryPoint()+".asm"),body);count++;
  }}finally{dec.dispose();}
  println("HONOR_STATIC_FUNCTIONS "+count);
 }
}

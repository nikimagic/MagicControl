import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import java.nio.file.*;
public class ExportNativeProtector extends GhidraScript {
 public void run() throws Exception {
  var out=Paths.get(getScriptArgs()[0]);Files.createDirectories(out);
  var decompiler=new DecompInterface();decompiler.openProgram(currentProgram);
  try {
   var functions=currentProgram.getFunctionManager().getFunctions(true);
   int count=0;
   while(functions.hasNext()) {
    var function=functions.next();var result=decompiler.decompileFunction(function,15,monitor);
    if(result.decompileCompleted())Files.writeString(out.resolve(function.getEntryPoint()+".c"),result.getDecompiledFunction().getC());
    else Files.writeString(out.resolve(function.getEntryPoint()+".error.txt"),result.getErrorMessage());
    count++;if(count%25==0)println("PROTECTOR_PROGRESS functions="+count);
   }
   println("PROTECTOR_EXPORT functions="+count);
  } finally {decompiler.dispose();}
 }
}

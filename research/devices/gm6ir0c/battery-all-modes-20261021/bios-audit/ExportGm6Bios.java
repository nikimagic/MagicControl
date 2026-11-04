import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.scalar.Scalar;
import java.nio.file.*;
public class ExportGm6Bios extends GhidraScript {
 public void run() throws Exception {
  var out=Paths.get(getScriptArgs()[0],currentProgram.getName());Files.createDirectories(out);
  var decompiler=new DecompInterface();decompiler.openProgram(currentProgram);
  var listing=new StringBuilder();var scalarHits=new StringBuilder();
  var instructions=currentProgram.getListing().getInstructions(true);
  while(instructions.hasNext()){
   var ins=instructions.next();listing.append(ins.getAddress()+" "+ins+"\n");
   for(int operand=0;operand<ins.getNumOperands();operand++)for(var obj:ins.getOpObjects(operand))if(obj instanceof Scalar){
    long v=((Scalar)obj).getUnsignedValue();if(v==0x7a6L||v==0x7b9L||v==0x7d0L||v==0xfe410000L||v==0xfe4107a6L||v==0xfe4107b9L||v==0xfe4107d0L){
     var f=getFunctionContaining(ins.getAddress());scalarHits.append(ins.getAddress()+" "+ins+" ; function="+(f==null?"?":f.getEntryPoint())+"\n");
    }
   }
  }
  Files.writeString(out.resolve("instructions.txt"),listing.toString());Files.writeString(out.resolve("scalar-hits.txt"),scalarHits.toString());
  int count=0;long deadline=System.nanoTime()+180000000000L;
  try{
   var functions=currentProgram.getFunctionManager().getFunctions(true);
   while(functions.hasNext()&&System.nanoTime()<deadline){
    var f=functions.next();var r=decompiler.decompileFunction(f,10,monitor);
    if(r.decompileCompleted())Files.writeString(out.resolve(f.getEntryPoint()+".c"),r.getDecompiledFunction().getC());
    else Files.writeString(out.resolve(f.getEntryPoint()+".error.txt"),r.getErrorMessage());
    count++;
   }
  }finally{decompiler.dispose();}
  println("GM6_BIOS_STATIC_EXPORT "+currentProgram.getName()+" functions="+count);
 }
}

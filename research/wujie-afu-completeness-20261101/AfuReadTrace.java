// File-only Ghidra analysis of a saved AFU PE. Never executes the imported PE.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import java.nio.file.*;
import java.util.*;
public class AfuReadTrace extends GhidraScript {
 public void run() throws Exception {
  Path out=Paths.get(getScriptArgs()[0]);Files.createDirectories(out);
  var funcs=new TreeMap<String,ghidra.program.model.listing.Function>();
  var refs=new StringBuilder();
  for(long va:new long[]{0x112c458L,0x112c450L,0x1125458L,0x111f930L,0x111f920L,0x110cf10L}){
   var a=toAddr(va);refs.append("\nTARGET "+a+"\n");
   for(var ref:getReferencesTo(a)){
    var f=getFunctionContaining(ref.getFromAddress());refs.append(ref+" function="+f+"\n");
    if(f!=null)funcs.put(f.getEntryPoint().toString(),f);
   }
  }
  var save=getFunctionAt(toAddr(0x107c310L));funcs.put(save.getEntryPoint().toString(),save);
  var dc=new DecompInterface();dc.openProgram(currentProgram);
  try{for(var f:funcs.values()){
   var d=dc.decompileFunction(f,30,monitor);
   Files.writeString(out.resolve(f.getEntryPoint()+".c"),d.decompileCompleted()?d.getDecompiledFunction().getC():d.getErrorMessage());
   var lines=new StringBuilder();var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();lines.append(i.getAddress()+" "+i+"\n");}
   Files.writeString(out.resolve(f.getEntryPoint()+".asm"),lines);
  }}finally{dc.dispose();}
  Files.writeString(out.resolve("references.txt"),refs.toString());
  Files.writeString(out.resolve("scope.txt"),"Saved AFU PE only; imported_PE_executed=false; functions="+funcs.size());
 }
}

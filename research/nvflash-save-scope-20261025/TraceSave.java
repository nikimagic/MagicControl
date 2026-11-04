// Read-only navigation/decompilation of a saved executable; never run NVFlash.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import java.nio.file.*;
import java.util.*;
public class TraceSave extends GhidraScript {
    public void run() throws Exception {
        var out=Paths.get(getScriptArgs()[0]);Files.createDirectories(out);
        var functions=new TreeMap<String,ghidra.program.model.listing.Function>();
        var references=new StringBuilder();
        for(var target:getScriptArgs()[1].split(",")){
            var parts=target.split(":");var address=toAddr(parts[1]);
            if(parts[0].equals("f")){
                var f=getFunctionContaining(address);if(f!=null)functions.put(f.getEntryPoint().toString(),f);
            }else{
                var end=parts[0].equals("r")?toAddr(parts[2]):address;
                for(var at=address;at.compareTo(end)<=0;at=at.add(parts[0].equals("r")?8:1)){
                    for(var ref:getReferencesTo(at)){
                        var f=getFunctionContaining(ref.getFromAddress());references.append(at+" <- "+ref.getFromAddress()+" "+f+"\n");
                        if(f!=null)functions.put(f.getEntryPoint().toString(),f);
                    }
                }
            }
        }
        var decompiler=new DecompInterface();decompiler.openProgram(currentProgram);
        try{for(var entry:functions.entrySet()){
            var f=entry.getValue();var r=decompiler.decompileFunction(f,45,monitor);
            Files.writeString(out.resolve(entry.getKey()+".c"),r.decompileCompleted()?r.getDecompiledFunction().getC():r.getErrorMessage());
            var listing=new StringBuilder();var it=currentProgram.getListing().getInstructions(f.getBody(),true);int n=0;
            while(it.hasNext()&&n++<100000){var i=it.next();listing.append(i.getAddress()+" "+i+"\n");}
            Files.writeString(out.resolve(entry.getKey()+".asm"),listing);
            var callers=new StringBuilder();for(var ref:getReferencesTo(f.getEntryPoint()))callers.append(ref+" "+getFunctionContaining(ref.getFromAddress())+"\n");
            Files.writeString(out.resolve(entry.getKey()+".refs.txt"),callers);
        }}finally{decompiler.dispose();}
        Files.writeString(out.resolve("refs.txt"),references);
        Files.writeString(out.resolve("summary.txt"),"functions="+functions.size()+"\n");
    }
}

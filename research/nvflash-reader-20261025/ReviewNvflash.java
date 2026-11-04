// Static analysis only. The imported vendor binary is never executed.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import java.nio.file.*;
import java.util.*;
public class ReviewNvflash extends GhidraScript {
    public void run() throws Exception {
        var out=Paths.get(getScriptArgs()[0]);Files.createDirectories(out);
        var targets=new TreeMap<String,ghidra.program.model.listing.Function>();
        var refs=new StringBuilder();
        for(var value:getScriptArgs()[1].split(",")){
            var address=toAddr(value);
            for(var ref:getReferencesTo(address)){
                var f=getFunctionContaining(ref.getFromAddress());
                refs.append(address+" <- "+ref.getFromAddress()+" "+f+"\n");
                if(f!=null)targets.put(f.getEntryPoint().toString(),f);
            }
        }
        for(var name:List.of("CreateServiceW","CreateServiceA","StartServiceW","StartServiceA","DeleteService","CM_Disable_DevNode")){
            var it=currentProgram.getSymbolTable().getSymbols(name);
            while(it.hasNext())for(var ref:getReferencesTo(it.next().getAddress())){
                var f=getFunctionContaining(ref.getFromAddress());
                refs.append(name+" <- "+ref.getFromAddress()+" "+f+"\n");
                if(f!=null)targets.put(f.getEntryPoint().toString(),f);
            }
        }
        var d=new DecompInterface();d.openProgram(currentProgram);
        try{for(var item:targets.entrySet()){
            var f=item.getValue();var result=d.decompileFunction(f,25,monitor);
            Files.writeString(out.resolve(item.getKey()+".c"),result.decompileCompleted()?result.getDecompiledFunction().getC():result.getErrorMessage());
            var callers=new StringBuilder();for(var ref:getReferencesTo(f.getEntryPoint()))callers.append(ref+" "+getFunctionContaining(ref.getFromAddress())+"\n");
            Files.writeString(out.resolve(item.getKey()+".refs.txt"),callers);
        }}finally{d.dispose();}
        Files.writeString(out.resolve("refs.txt"),refs);
    }
}

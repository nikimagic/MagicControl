// Static analysis of a saved PE; never execute the imported program.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import java.nio.file.*;
import java.util.*;
public class ExportFpt extends GhidraScript {
    public void run() throws Exception {
        var out=Paths.get(getScriptArgs()[0]);Files.createDirectories(out);
        var functions=new TreeMap<String,ghidra.program.model.listing.Function>();
        var references=new StringBuilder();
        for(var text:getScriptArgs()[1].split(",")){
            var address=currentProgram.getImageBase().add(Long.parseLong(text,16));
            var function=getFunctionContaining(address);
            if(function!=null)functions.put(function.getEntryPoint().toString(),function);
        }
        for(var name:getScriptArgs().length>2?List.<String>of():List.of("DeviceIoControl","CreateServiceW","StartServiceW","DeleteService","CreateFileW")){
            var iterator=currentProgram.getSymbolTable().getSymbols(name);
            while(iterator.hasNext()){
                var symbol=iterator.next();
                for(var ref:getReferencesTo(symbol.getAddress())){
                    var function=getFunctionContaining(ref.getFromAddress());
                    references.append(name+" "+symbol.getAddress()+" <- "+ref.getFromAddress()+" "+function+"\n");
                    if(function!=null)functions.put(function.getEntryPoint().toString(),function);
                }
            }
        }
        var decompiler=new DecompInterface();decompiler.openProgram(currentProgram);
        try {
            for(var entry:functions.entrySet()){
                var function=entry.getValue();var r=decompiler.decompileFunction(function,30,monitor);
                Files.writeString(out.resolve(entry.getKey()+".c"),"/* Exploratory decompiler output; verify instructions. */\n"+
                    (r.decompileCompleted()?r.getDecompiledFunction().getC():r.getErrorMessage()));
                var listing=new StringBuilder();var instructions=currentProgram.getListing().getInstructions(function.getBody(),true);
                while(instructions.hasNext()){var instruction=instructions.next();listing.append(instruction.getAddress()+" "+instruction+"\n");}
                Files.writeString(out.resolve(entry.getKey()+".asm"),listing);
                var callers=new StringBuilder();for(var ref:getReferencesTo(function.getEntryPoint()))callers.append(ref+" "+getFunctionContaining(ref.getFromAddress())+"\n");
                Files.writeString(out.resolve(entry.getKey()+".refs.txt"),callers);
            }
        } finally {decompiler.dispose();}
        Files.writeString(out.resolve("import-refs.txt"),references);
        Files.writeString(out.resolve("summary.txt"),"program="+currentProgram.getName()+"\nfunctions="+
            currentProgram.getFunctionManager().getFunctionCount()+"\nexports="+functions.size()+"\n");
    }
}

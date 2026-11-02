// Per-function metadata for the inventory: one line per function,
//   addr<TAB>name<TAB>bytes<TAB>callers<TAB>callees(comma list)<TAB>imports(comma list)<TAB>strings(|-separated)
// Usage: ghidra script run tools/ghidra/DumpMeta.java --project oni --program oni.exe -- /abs/path/re/meta.tsv
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import java.io.*;
import java.util.*;
public class DumpMeta extends GhidraScript {
  public void run() throws Exception {
    String[] args = getScriptArgs();
    PrintWriter w = new PrintWriter(new FileWriter(args[0]));
    FunctionManager fm = currentProgram.getFunctionManager();
    ReferenceManager rm = currentProgram.getReferenceManager();
    Listing lst = currentProgram.getListing();
    for (Function f : fm.getFunctions(true)) {
      if (f.isExternal()) continue;
      Set<String> callees = new TreeSet<>(), imps = new TreeSet<>();
      List<String> strs = new ArrayList<>();
      for (Function c : f.getCalledFunctions(monitor)) {
        if (c.isExternal() || c.isThunk()) {
          Function t = c.isThunk() ? c.getThunkedFunction(true) : c;
          imps.add(t != null ? t.getName() : c.getName());
        } else callees.add(c.getEntryPoint().toString());
      }
      InstructionIterator it = lst.getInstructions(f.getBody(), true);
      while (it.hasNext()) {
        Instruction ins = it.next();
        for (Reference r : ins.getReferencesFrom()) {
          Address to = r.getToAddress();
          Data d = lst.getDataAt(to);
          if (d != null && d.hasStringValue() && strs.size() < 12) {
            String s = String.valueOf(d.getValue()).replace("\t"," ").replace("\n","\\n").replace("|","/");
            if (s.length() > 80) s = s.substring(0, 80);
            strs.add(s);
          }
          Symbol sym = currentProgram.getSymbolTable().getPrimarySymbol(to);
          if (sym != null && sym.isExternal()) imps.add(sym.getName());
          if (sym != null && sym.getName().startsWith("PTR_") ) {
            // indirect import call: pointer into the IAT
            Data p = lst.getDataAt(to);
            if (p != null && p.getValue() instanceof Address) {
              Symbol t = currentProgram.getSymbolTable().getPrimarySymbol((Address)p.getValue());
              if (t != null && t.isExternal()) imps.add(t.getName());
            }
          }
        }
      }
      int callers = 0;
      for (Reference r : rm.getReferencesTo(f.getEntryPoint())) callers++;
      w.println(f.getEntryPoint() + "\t" + f.getName() + "\t" + f.getBody().getNumAddresses() + "\t" + callers + "\t" +
        String.join(",", callees) + "\t" + String.join(",", imps) + "\t" + String.join("|", strs));
    }
    w.close();
    println("done");
  }
}

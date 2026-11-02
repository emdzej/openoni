// Creates functions at the entry points listed in a file (one hex address per line, '#' comments, a
// second column for a note): code that is only reached through tables of pointers, which neither the
// auto-analysis nor FillGaps.java finds. Disassembles first if needed. Idempotent.
// Usage: ghidra script run tools/ghidra/CreateFunctions.java --project oni --program oni.exe -- <entries.tsv>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
public class CreateFunctions extends GhidraScript {
  public void run() throws Exception {
    Listing l = currentProgram.getListing();
    int made = 0, had = 0;
    for (String line : Files.readAllLines(Paths.get(getScriptArgs()[0]))) {
      line = line.trim();
      if (line.isEmpty() || line.startsWith("#")) continue;
      Address a = toAddr(Long.parseLong(line.split("\\s+")[0].replaceFirst("^0x", ""), 16));
      if (getFunctionAt(a) != null) { had++; continue; }
      Function in = getFunctionContaining(a);
      if (in != null) { println("inside " + in.getName() + ": " + a); continue; }
      if (l.getInstructionAt(a) == null) {
        Data d = l.getDataContaining(a);
        if (d != null) l.clearCodeUnits(d.getMinAddress(), d.getMaxAddress(), false);
        disassemble(a);
      }
      if (createFunction(a, null) != null) made++;
      else println("could not create " + a);
    }
    println("created " + made + ", already there " + had);
  }
}

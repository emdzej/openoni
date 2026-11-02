// Applies a names file to the current program: one function per line,
//   addr<TAB>name<TAB>module<TAB>confidence<TAB>description
// renames the function at addr (creating it if Ghidra missed it) and sets its plate comment to
// "[module] description". Lines starting with '#' and blank lines are skipped.
// Usage: ghidra script run tools/ghidra/ApplyNames.java --project oni --program oni.exe -- /abs/path/names.tsv
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.nio.file.*;
public class ApplyNames extends GhidraScript {
  public void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length < 1) { printerr("usage: ApplyNames.java <names.tsv>"); return; }
    int ok = 0, created = 0, bad = 0;
    for (String line : Files.readAllLines(Paths.get(args[0]))) {
      if (line.isBlank() || line.startsWith("#")) continue;
      String[] f = line.split("\t", -1);
      if (f.length < 2) { bad++; continue; }
      Address a = toAddr(Long.parseLong(f[0].replaceFirst("^0x", ""), 16));
      Function fn = getFunctionAt(a);
      if (fn == null) { fn = createFunction(a, null); if (fn != null) created++; }
      if (fn == null) { printerr("no function at " + f[0]); bad++; continue; }
      try { fn.setName(f[1].trim(), SourceType.USER_DEFINED); }
      catch (Exception e) { printerr(f[0] + " " + f[1] + ": " + e.getMessage()); bad++; continue; }
      if (f.length >= 5) fn.setComment("[" + f[2].trim() + "] " + f[4].trim());
      ok++;
    }
    println("renamed " + ok + ", created " + created + ", failed " + bad);
  }
}

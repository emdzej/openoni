// Creates functions in code gaps the auto-analysis left undefined: at each 16-byte aligned address after
// a RET (or INT3/NOP padding) inside .text that isn't already in a function or defined data, disassembles
// and creates a function. Prints how many were created.
// Usage: ghidra script run tools/ghidra/FillGaps.java --project oni --program oni.exe
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
public class FillGaps extends GhidraScript {
  public void run() throws Exception {
    MemoryBlock text = currentProgram.getMemory().getBlock(".text");
    Listing l = currentProgram.getListing();
    int made = 0;
    for (int pass = 0; pass < 3; pass++) {
      Address a = text.getStart();
      while (a.compareTo(text.getEnd()) < 0) {
        if (a.getOffset() % 16 == 0 && getFunctionContaining(a) == null && l.getDataContaining(a) == null) {
          byte prev = getByte(a.subtract(1));
          byte cur = getByte(a);
          boolean after = prev == (byte)0xc3 || prev == (byte)0xcc || prev == (byte)0x90 ||
                          (getByte(a.subtract(3)) == (byte)0xc2);
          if (after && cur != (byte)0xcc && cur != (byte)0x00 && cur != (byte)0x90) {
            if (l.getInstructionAt(a) == null) disassemble(a);
            if (l.getInstructionAt(a) != null && createFunction(a, null) != null) made++;
          }
        }
        a = a.add(16 - (a.getOffset() % 16));
      }
    }
    println("created " + made + ", total " + currentProgram.getFunctionManager().getFunctionCount());
  }
}

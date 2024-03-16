
# Usage:
# Please setup the GDB to load scripts from this directory:
#       add-auto-load-safe-path ~/git/srs/trunk/research/st
#       add-auto-load-safe-path ~/srs/research/st
# Or you can simply copy the .gdbinit to yours:
#       cp ~/git/srs/trunk/research/gdb/.gdbinit ~/.gdbinit
# Then build and debug with GDB.

tui enable
echo "Enter TUI by default, use tui disable to quit.\n"

set pagination off
echo "Set pagination off ok.\n"

set disassembly-flavor intel
echo "Set disassembly flavor to intel ok.\n"

b *main
b *foo
b *_st_thread_main
b _st_md_cxt_restore
b *st_thread_create
echo "Set breakpoint at throw *foo.\n"

run
echo "Start running program ok.\n"

tui layout next
tui layout next
winheight src -5
winheight asm +10
echo "Set winheight asm smaller ok.\n"

info registers
echo "Show registers ok.\n"

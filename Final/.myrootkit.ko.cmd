cmd_/usr/rootkit/CS558/Final/myrootkit.ko := ld -r -m elf_i386 -T /usr/src/linux-headers-4.6.0-kali1-common/scripts/module-common.lds --build-id  -o /usr/rootkit/CS558/Final/myrootkit.ko /usr/rootkit/CS558/Final/myrootkit.o /usr/rootkit/CS558/Final/myrootkit.mod.o

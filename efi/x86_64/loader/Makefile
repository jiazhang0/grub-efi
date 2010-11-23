
all : switch2.h

bin_to_h: bin_to_h.c
	$(CC) $(CFLAGS) -o $@ $<

switch2.h : switch2.bin bin_to_h
	./bin_to_h < $< > $@

switch2.bin : switch2
	objcopy -O binary $< $@

switch2 : switch2.o
	ld.bfd -Ttext-segment=0x688 --init _start -o $@ $<

switch2.o : switch.S
	as -march=generic64 -defsym _start=0 -o $@ $<

clean :
	@rm -vf switch2.o switch2 switch2.bin switch2.h bin_to_h

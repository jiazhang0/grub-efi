
all : switch.h

bin_to_h: bin_to_h.c
	$(CC) $(CFLAGS) -o $@ $<

switch.h : switch.bin bin_to_h
	./bin_to_h < $< > $@

switch.bin : switch
	objcopy -O binary $< $@

switch : switch.o
	ld.bfd -Ttext-segment=0x688 --init _start -o $@ $<

switch.o : switch.S
	as -march=generic64 -defsym _start=0 -o $@ $<

clean :
	@rm -vf switch.o switch switch.bin switch.h bin_to_h

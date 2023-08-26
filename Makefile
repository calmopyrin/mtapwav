all:
	make wav2mtap
	make mtap2wav
	
wav2mtap:
	gcc mtap.c pcmwav.c wav2tap.c -lm -o wav2mtap -O3

mtap2wav:
	gcc mtap.c pcmwav.c wav2tap.c -lm -o mtap2wav -O3

clean:
	rm -f *.o
	rm -f wav2mtap
	rm -f mtap2wav

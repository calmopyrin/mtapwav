# mtapwav

mtapwav is a collection of Commodore MTAP image <-> WAV conversion tools, namely tap2wav and wav2tap. As the name implies these tools are meant for conversion across these two image formats.

# tap2wav

This tool converts MTAP images to the PCM WAV audio format. Default format is mono 8-bit 44.1 kHz PCM. The tool performs a simple DC removal and LP filtering as well.
A special 1-bit format is also supported that retains the characteristics of the signals represented in the original MTAP image.
There is a possibility to invert the signal and change the sampling frequency.

# wav2tap

This is a more sophisticated tool that is able to convert WAV audio to MTAP. It supports various signal detection algorithms and thresholds but performs no filtering. Supported detection methods: edge detect, hysteresis, zero crossing, differential and their combinations. You can choose among these as well as set the detection threshold and invert the input signal with command line switches.

# Overview

This is a synth that generates waveforms by adding sines together.

It can be inefficient to generate waveforms this way for low frequencies, but it's fun and easy to understand. It's also quite easy to avoid aliasing.

Ring modulation can be applied on each of the sines to change the sound's texture, and most parameters are controllable at a sample-rate level via ADSR envelopes and LFOs in many different combinations.

A shaped "amplitude boost" that emulates a sharp filter cutoff can also be swept along the waveform.

# Generation equation

Three classic synth waveforms can be generated by summing sines according to the following equations:

```
saw
j=1 -1**j * 1/j * sin(2*pi*j*f*t)

square
j=1 1/(2*j-1) * sin(2*pi*(2*j-1)*f*t)

tri
j=0 -1**j * (2*j+1) * sin(2*pi*j*f*t)
```

This synth uses a unified equation that can cover all the above cases, as follows:

	j=start powbase**(j*expmul) * (scalemul*j+scaleoff)**scaleexp * sin(2*pi*(scalemul*j+scaleoff)*f*t)
	
By configuring the parameters correctly, this equation can generate each of the above waveforms, and maybe some other interesting ones besides.

# Some notes on performance

GCC is generally very good at vectorising loops when `-ffast-math` and `-ftree-loop-vectorize` is enabled, but for the important loops I'm often using the vector types and builtins just to be sure that they are being vectorized.

Julien Pommier's SSE math functions were even faster than GCC's trig function vectorisation. The biggest speed gain came from this.

I tried an implementation using look up tables to generate sines, and even without interpolation it was only about as fast as the version using SSE sines.

An example of how to examine generated assembly code to see how much has been vectorised:

	gcc -DHAVE_CONFIG_H -I. -I.. -pthread -I../buzztrax-stable/include -I/usr/include/gstreamer-1.0 -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include -O2 -ffast-math -lm -ftree-loop-vectorize -std=gnu99 -Werror -Wno-error=unused-variable -Wall -Wshadow -Wpointer-arith -Wstrict-prototypes -fvisibility=hidden -g -fPIC -DPIC -Wa,-adhln ../src/additive.c
	
I.e. copy the build command that `make` generates and add `-Wa,-adhln`.

Laster, Julien's functions were replaced by the same Cephes library functions translated to use vector intrinsics. These were found to perform just as fast.

Disabling denormal floats gave a minor performance boost.

Using v4sf consts instead of float literals in vectorised code made no difference -- the compiler is smart enough to choose the best representation and isn't creating overhead in converting vectors back and forth.

In inner loops, it's still generally better to avoid heavy computation using branches rather than trying to avoid all branching.

Striping the buffers for the "s-rate" control parameters provided no benefit, and neither did calculating all overtones for each sample before moving to the next sample (instead of calculating all samples for each overtone before moving to the next overtone.) The process doesn't really seem bound by memory accesses, or the data fits in the cache anyway.

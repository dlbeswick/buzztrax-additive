```
saw
j=1 -1**j * 1/j * sin(2*pi*j*f*t)

square
j=1 1/(2*j-1) * sin(2*pi*(2*j-1)*f*t)

tri
j=0 -1**j * (2*j+1) * sin(2*pi*j*f*t)
```

Unified:

```
saw
j=1 -1**(j*1) * (1*j+0)**-1 * sin(2*pi*(1*j+0)*f*t)

square
j=1 1**(j*0) * (2*j-1)**-1 * sin(2*pi*(2*j-1)*f*t)

tri
j=0 -1**(j*1) * (2*j+1)**1 * sin(2*pi*(2*j+1)*f*t)

j=start powbase**(j*expmul) * (scalemul*j+scaleoff)**scaleexp * sin(2*pi*(scalemul*j+scaleoff)*f*t)
```

```
		  (*buf++) +=
			hscale_amp *
			(-0.5f + pow(sin01(hscale_freq * freq * t), ((1+(sin01(t * self->pwm_rate))*self->pwm_depth))));
```

// Does each overtone need its own accumulator for smooth playback?
// What's happening here:
// There will be a small amount of error after the accumulator is wrapped.
// That error will then be multiplied if "hscale_freq * freq" is applied to t.
// I guess that turns out to be substantial.
// When a pure accumlator is used, only the original, tiny error makes it into the signal.
// Problem is only seen with fractional frequencies.

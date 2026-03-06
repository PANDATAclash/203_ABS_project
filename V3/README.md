This PCB features the LMR14050SDDAR buck converter, which steps down the input voltage from the motorcycle and provides 5 V for the Teensy 4.1 and other 5 V modules. Several capacitors and excessive filtering components were removed from the design because they were either unnecessary or could negatively affect the system’s response time.

The AP2112K-3.3TRG1 regulator, which previously stepped 5 V down to 3.3 V, was also removed. Instead, the 3.3 V supply is now taken directly from the microcontroller.

At this stage, the Hall sensor interface circuit has not yet been included. We are still waiting for the motorcycle to be delivered so we can test and read the OEM Hall sensors on the bike before finalizing and adding that part of the design. The motorcycle model is a 2018 Yamaha Tracer 7.

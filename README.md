# Plasma-Torch-Height-Controller-Standalone
Plasma Torch Height Controller Standalone... accepts Z axis direction and step signals from a controller and allows the signals to pass through when the Arc OK signal from the plasma cutter indicates it is not currently cutting. It uses code from swolebro, the legend who wrote a super quick THC see here (https://github.com/swolebro/swolebro-youtube/blob/master/arduino/thc/thc.ino) and also bits and pieces from here moose4621 (https://github.com/moose4621/Plasma_THC_standalone)

FYI I am in the process of setting up a unimig cut45 plasma cutter on my cnc. Hopefully within the next couple of months I will be able to do some live testing of this THC.

I have only tested this on the wokwi online simulator so far - see here https://wokwi.com/projects/339032968522629715 the labelling system leaves a bit to be desired but each components ID should enlighten you as to what is what. The project will show you how to connect all the basic components up, ie without optos etc.

I have no idea if this will be quick enough to work properly during use. So do not be surprised if it does not do what you want. 

I am no programmer or electronic giru so take precautions if you are considering testing it out. Swolebro has some great and entertainly info on his CNC build on youtube regarding some electronics that should be considered ie optos and volt regulator etc and also how his portion of the code works. (electronics  -- https://www.youtube.com/watch?v=lkKd5P8oH5Q&list=PL9xPdBFt5g3Q6TkuhhfQmQNm6TdvNkPuX&index=20) & (code -- https://www.youtube.com/watch?v=nmXoZt423WI&list=PL9xPdBFt5g3Q6TkuhhfQmQNm6TdvNkPuX&index=21)

If anyone can make use of this and has any good feedback that'd be muchly appreciated.


-----controls on the THC-------------

-THC on switch

-2 x 10k pots (for setting volt and stepper speed%)

-LCD

-THC on LED

-THC raise LED

-THC lower LED


-----connections to THC--------

- direction output from controller
- step output from controller
- GND from controller

- direction output to stepper driver
- step output to stepper driver
- GND to stepper driver

- ark OK output from plasma cutter (with THC GND as it is a dry contact) 
- +torch voltage from plasma cutter (50:1 voltage divider)
- -torch voltage from plasma cutter



-----how it functions (or should)-----

- when booted (without engageThcSwitch being in the on position) an LCD will show the current voltage and stepper speed% settings
- you can now adjust both those settings
- Once set, use the engageThcSwitch to turn on the THC (this locks the settings, currently ONLY a reset with the switch off will allow resetting these)
- The green LED indicates THC is on
- when plasma indicates arc ok - step and direction pulses will be sent to the stepper driver.
- when plasma indicates arc is not ok - step and direction pulses will be bypassed through the THC.


note:

keep in mind, this program DOES NOT take into account de-acceleration whether it happens in corners etc or passing over kerfs etc. I am hoping I may be able to some what sort these types of issues once I am able to get it working in anger. Failing that I may have to go the linuxcnc route, but currently the learning curve is too steep for my available time, too many projects.

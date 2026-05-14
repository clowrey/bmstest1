## Safety mechanisms

### Events

There are four levels of event severity:

- INFO
- WARNING
- CRITICAL
- FATAL

**INFO** events are informational, and don't indicate a problem.

**WARNING** events indicate a potential problem which is not yet serious.

**CRITICAL** events signal imminent contactor opening and escalate to FATAL
after a timeout (which differs by event type). To catch intermittent (flapping)
faults, a cumulative time buffer increments while the event is active and
decrements when cleared; if the buffer fills, the event escalates to FATAL.

**FATAL** events immediately trigger the contactor opening sequence, and require
manual intervention to reset. Unlike the other event types, FATAL events do not
clear when the triggering condition stops.


### Current limits

The BMS continuously calculates the maximum allowed charge and discharge current
in its present state, and these limits are transmitted to the inverter.

The limits are the lesser of several different calculations:

**Temperature** derating, which linearly tapers the maximum current such that it
reaches zero at the maximum temperature limit. The limits are different for
charging and discharging, according to the cell chemistry.

**State of charge** derating




### Voltage limits

There are six separate voltage limits, which are defined in reference to the
voltage of an individual cell. These limits operate progressively, and are
designed to ensure safety whilst making it usually possible to recover a pack
that has gone beyond the normal voltage range.

#### Working limits

The battery's operating range is defined by the working voltage limits,
representing 0% and 100% State of Charge (SoC). 

The BMS will taper the charge current at the top to attempt to keep the cell
'virtual OCV' at the working limit, which allows for absorption charge using
current limits. This means the terminal voltage can go slightly higher than the
working limits.

The discharge current limit will drop to zero at the lower limit, with 100mV
hysteresis band before it recovers.

Since some inverters prevent discharging below a specific threshold (e.g., 10%),
the Minimum SoC setting allows you to define a percentage that will correspond
to the lower voltage limit.

#### Soft voltage limits

If a cell's voltage goes outside the soft limits, the BMS immediately issues a
WARNING. It will also restrict current to prevent further charging (at the upper
limit) or discharging (at the lower limit).

The BMS will disconnect the battery if:
- A current continues to flow against these restrictions.
- The state persists for more than one hour.

This gives you a chance to correct the problem - if the battery has
disconnected, you can restart the BMS, and will have one hour to
charge/discharge the battery back into the normal range.


#### Hard voltage limits

If a cell exceeds its hard voltage limits, the BMS will immediately disconnect
the battery. These limits are set to prevent permanent damage to the cell
chemistry. Recovery would require bypassing or manually energizing the
contactors. 

Be careful if you decide to manually recover or recharge the battery, as it may
be damaged, and you may need to bypass safety mechanisms to do so.


### Response time

If a critical condition arises there is usually a short timeout (a second or
two) to see if it clears, after which the contactor opening sequence will start.
This sets the current limits to zero, and then waits for the current to fall and
opens the contactors. If the current hasn't fallen below the threshold after two
seconds, the contactors open anyway.

Thus the BMS will react to fault conditions within a timescale of seconds,
whilst avoiding nuisance trips due to brief glitches, and avoiding contactor
damage where possible. External safety equipment should be used for more
time-critical protection against overcurrent (fuses) or leakage (an RCD).





## Internal architecture

The BMS has a conventional single-threaded loop architecture, with a 50Hz loop
frequency. Sensor reading and communication is performed asynchronously
using interrupts/DMA to avoid blocking where possible.






The main loop of the BMS runs every 20 milliseconds. It will therefore take at
least 20ms for the BMS to react to a change, up to several multiples of 20ms if
the signal propagates through the model in stages.



Most operations performed in the main loop should easily complete within the
20ms deadline. However the BMS performs occasional flash memory writes which can
take much longer (the worst case block-erase time of the SPI flash chip is 2
seconds). The normal WARNING that occurs if a loop tick overruns is suppressed
when flash memory operations are performed.

A system-wide watchdog timer is set up with a 5 second deadline, which gets
reset every tick. If the main loop stalls for more than 5 seconds, the chip will
reboot and raise a WARNING event.


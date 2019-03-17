
h2. What it does : 

1. Stops Windows Audio Endpoint builder and dependent on lid close while on battery, or on power source disconnection while lid closed.
1. Starts stopped services on lid open 

h2. How to use : 

1. Download https://github.com/AlexMKX/LenovoSleepFix/raw/master/Release/OnSleep.exe and save it where it will reside (it don't copy itself anywhere)
1. Open elevated command prompt
1. run OnSleep.exe --install
1. Check events in event log for any problems.
1. run OnSleep.exe --help for additional options.

h2. Release notes:

You MUST use Intel High Definition Audio (10.0.17763.1) or later driver, instead of Realtek High Definition Audio(SST), because last one don't recovers audio functionality after service start/stop and you'll have deaf and dumb laptop after wakeup.


h2. Todo:

implement custom programs/scripts on sleep/wake for additional functionality



h2. License : 

1. GPL
1. No warranty.
1. No indemnification
1. Use at your own risk
1. Computer manufacturers (especially Lenovo) are not allowed to redistribute or include this software anywhere without author's writtent contest

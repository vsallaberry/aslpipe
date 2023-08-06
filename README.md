
## aslpipe
--------------

* [Overview](#overview)
* [System Requirements](#system-requirements)
* [Details](#details)
* [Contact](#contact)
* [License](#license)

## Overview
**aslpipe** is a tiny macOS utility redirecting all outputs to syslog (or asl).

## System requirements
- macOS, gcc/clang for compilation.

## Details
Here is an example of ASL configuration (/etc/asl/com.vs.utils.conf)  
This will capture 'info' or lower level messages from 'local2' facility:  
For process pf run as root, log in /var/log/pf.log, and /var/log/me.log otherwise:  
    > /var/log/pf.log mode=0640 gid=20 rotate=seq compress file_max=75M all_max=350M format='$((Time)(local.6)) $((Level)(str)) [$(Sender)] $(MessageV)'
    > /var/log/me.log mode=0640 gid=20 rotate=seq compress file_max=75M all_max=150M format='$((Time)(local.6)) $((Level)(str)) [$(Sender)] $(MessageV)'
    ? [! Facility local2] skip
    ? [= Facility local2] claim
    ? [> Level info] ignore
    =dup_delay 1
    ? [= Sender pf] [UID 0] file /var/log/pf.log
    ? [! Sender pf] file /var/log/me.log
Examples:  
    $ while true; do echo "Hello"; sleep 1; done | aslpipe -F local2 -K MessageV 
    $ aslpipe -K MessageV -F local2 -l 6 -S pf "${tcpdump}" -k INPSDC -lnettti pflog0
written to display summary about different build log files, on different 
machines. Tested only with bash (even if the header is !/bin/sh).

## Contact
[vsallaberry@gmail.com]  
<https://github.com/vsallaberry/aslpipe>

## License
GPLv3 or later. See LICENSE file.

Copyright : Copyright (C) 2021-2023 Vincent Sallaberry


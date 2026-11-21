Host-side tests (PC, no hardware): they compile the REAL firmware sources with gcc.
 t.c unit tests; z.c time zone/DST engine (checked against Python's IANA database); cal.c calendar maths;
 harness.c end-to-end simulation of ESP AT module + DS3231 + EEPROM (74 checks);
 irtest.c pushes remote-control buttons through the real IR handler across the new screens (45 checks);
 shots.c / mainshots.c render the OLED screens.
They prove the logic; they do not replace testing on the real board.

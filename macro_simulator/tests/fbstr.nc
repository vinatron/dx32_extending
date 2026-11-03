O0012 (STRAIGHT-LINE PROGRAM, 30I FORMS)
G21 G90 G17
#1 = 10.
#2 = 17 MOD 5
#3 = [#1 + #2] * 2. / 4.
IF [[#1 GT 5.] AND [#2 EQ 2.]] THEN #4 = 1.
IF [#1 LT 5.] THEN #4 = 2.
G0 X[#1 + 2.] Y#3 Z5.
S1200 M3
G1 Z-1. F[#1 * 25.]
X[SQRT[#1 * #1 + #3 * #3]] Y0
G0 Z5. M5
M30

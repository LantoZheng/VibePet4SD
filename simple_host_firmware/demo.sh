# Demo script: logo -> wait -> face -> beep -> blink -> loop
SHOW logo
WAIT 1500
TEXT Hello from script!
WAIT 1000
SHOW face
BEEP 2400 80
WAIT 500
BEEP 1800 60
WAIT 1500
ANIM_PLAY blink
WAIT 6000
ANIM_STOP
SHOW status
TEXT Script done!
WAIT 2000
SHOW logo

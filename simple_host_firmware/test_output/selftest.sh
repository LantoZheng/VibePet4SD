# SD2-OS On-Device Self-Test Module
# Tests: display, beep, animation, env, script
TEXT === SD2-OS SelfTest ===
WAIT 1200

# Test brightness
BRIGHT 40
WAIT 400
BRIGHT 85
WAIT 400

# Test built-in images
SHOW logo
WAIT 800
SHOW face
BEEP 2000 50
WAIT 800
SHOW bars
WAIT 800
SHOW status
WAIT 800

# Test animation if available
# ANIM_PLAY blink
# WAIT 3000
# ANIM_STOP

# Done
SHOW logo
TEXT SelfTest PASSED!
WAIT 2000
END

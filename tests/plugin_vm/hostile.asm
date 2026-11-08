code

; Hand written code that C does not make. In this notation a store is: the address, then the value, then ASGN.

; Loops for ever with a jump to an address in a variable. The label is only the target of a jump forward (that is what makes it
; a place a program may jump to at all), the jump back is the one that counts.
export Hostile_JumpLoop
proc Hostile_JumpLoop 8 0
ADDRLP4 0
ADDRGP4 $top
ASGNP4
ADDRGP4 $top
JUMPV
LABELV $top
ADDRLP4 4
ADDRLP4 4
INDIRI4
CNSTI4 1
ADDI4
ASGNI4
ADDRLP4 0
INDIRP4
JUMPV
CNSTI4 0
RETI4
LABELV $1
endproc Hostile_JumpLoop 8 0

; Calls an address in the middle of a function, where the code that sets up the call is not.
export Hostile_CallMid
proc Hostile_CallMid 8 0
ADDRLP4 0
ADDRGP4 $mid
ASGNP4
ADDRGP4 $mid
JUMPV
LABELV $mid
ADDRLP4 4
ADDRLP4 0
INDIRP4
CALLI4
ASGNI4
CNSTI4 0
RETI4
LABELV $2
endproc Hostile_CallMid 8 0

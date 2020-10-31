	GBLS	_HAT
 [ :DEF:ARM32
_HAT	SETS	""
	MACRO
$a	MOVSPC $r,$c
$a	MOV$c	PC,$r
	MEND
 |
_HAT	SETS	"^"
	MACRO
$a	MOVSPC $r,$c
$a	MOV$c.S	PC,$r
	MEND
 ]
	END

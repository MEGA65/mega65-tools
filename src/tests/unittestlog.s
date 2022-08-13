;;
;; Unittest ca65 include for sending unittest messages via hypervisor to m65
;;

	ptrTest0	=	$FA


	.define TEST_START $f0
	.define TEST_SKIP $f1
	.define TEST_PASS $f2
	.define TEST_FAIL $f3
	.define TEST_ERROR $f4
	.define TEST_LOG $fd
	.define TEST_SETNAME $fe
	.define TEST_DONEALL $ff


__tests_out:
	.byte	$00

__ut_issueNum:
	.word	$0000

__ut_subissue:
	.byte	$00

;.A	=	<issue
;.X	=	>issue
;.Y	=	sub
;.Z	=	status
;void unit_test_report(unsigned short issue, unsigned char sub, unsigned char status)
unit_test_report:
		STA	$D643
		NOP

		TXA
		STA	$D643
		NOP

		TYA
		STA	$D643
		NOP

		TZA		; need to use STA, as this otherwise collides with HYPER_Z somehow
		STA	$D643
		NOP

		RTS

;.A	=	<msg
;.X	=	>msg
;.Y	=	cmd
;void _unit_test_msg(char *msg, char cmd)
_unit_test_msg:
		STA	ptrTest0
		STX	ptrTest0 + 1

		TYA
		TAZ

		LDA	#$00
		LDX	#$00
		LDY	#$00
		JSR	unit_test_report

		LDY	#$00
@loop:
		LDA	(ptrTest0), Y
		BEQ	@done

		STA	$D643
		NOP

		INW	ptrTest0
		JMP	@loop

@done:
		LDA	#92
		STA	$D643
		NOP

		RTS


;.A	=	<name
;.X	=	>name
;void unit_test_set_current_name(char *name)
unit_test_set_current_name:
		LDY	#TEST_SETNAME
		JSR	_unit_test_msg
		RTS


;.A	=	<name
;.X	=	>name
;void unit_test_log(char *msg)
unit_test_log:
		LDY	#TEST_LOG
		JSR	_unit_test_msg
		RTS


;.A	=	<testName
;.X	=	>testName
;.Y	=	<issueNum
;.Z	=	>issueNum
; subissue is initialized with 0 and incremented to 1 for the first test
;void unit_test_setup(char *testName, unsigned short issueNum)
unit_test_setup:
		PHA

		LDA	#$47
		STA	$D02F
		LDA	#$53
		STA	$D02F

		STY	__ut_issueNum
		STZ	__ut_issueNum + 1

		LDA	#$00
		STA	__ut_subissue

		PLA
		JSR	unit_test_set_current_name

		LDA	__ut_issueNum
		LDX	__ut_issueNum + 1
		LDY	__ut_subissue
		LDZ	#TEST_START
		JSR	unit_test_report

		INC	__ut_subissue

		RTS


;.A	=	<msg
;.X	=	>msg
; msg can be 0, then no message is logged
;void unit_test_ok(char *msg)
unit_test_ok:
		TAY
		STX	__tests_out
		ORA	__tests_out
		TYA
		BEQ	@nolog
		JSR	unit_test_log

@nolog:
		LDA	__ut_issueNum
		LDX	__ut_issueNum + 1
		LDY	__ut_subissue
		LDZ	#TEST_PASS
		JSR	unit_test_report

		INC	__ut_subissue

		RTS


;.A	=	<msg
;.X	=	>msg
; msg can be 0, then no message is logged
;void unit_test_fail(char *msg)
unit_test_fail:
		TAY
		STX	__tests_out
		ORA	__tests_out
		TYA
		BEQ	@nolog
		JSR	unit_test_log

@nolog:
		LDA	__ut_issueNum
		LDX	__ut_issueNum + 1
		LDY	__ut_subissue
		LDZ	#TEST_FAIL
		JSR	unit_test_report

		INC	__ut_subissue

		RTS


;.Y	set to 0 to use 0 as subisse, set to non-0 to use current subissue
;void unit_test_done(char zero_sub_issue) {
unit_test_done:
		CPY	#0
		BEQ	@nosubissue
		LDY	__ut_subissue
@nosubissue:
		LDA	__ut_issueNum
		LDX	__ut_issueNum + 1
		LDZ	#TEST_DONEALL
		JSR	unit_test_report

		RTS

proc run_serial_capture {command success_pattern failure_pattern description stage_timeout} {
	global qemu_spawn_id timeout
	set saved_timeout $timeout
	set timeout $stage_timeout
	set started_ms [clock milliseconds]
	set timestamp [clock format [clock seconds] -gmt 1 -format {%Y-%m-%dT%H:%M:%SZ}]
	puts "STAGE_BEGIN name={$description} utc=$timestamp timeout_s=$timeout"
	ensure_serial_root_shell

	# The serial shell has stty -echo. Split the completion token across printf
	# arguments as well, so its full value cannot occur in the submitted command.
	set completion_middle [format "%d_%d" [pid] [clock clicks]]
	set completion_marker "__SEYRIX_CAPTURE_DONE_${completion_middle}__"
	set completion_command "$command; printf '\\n%s%s%s\\n' '__SEYRIX_CAPTURE_DONE_' '$completion_middle' '__'"
	send -i $qemu_spawn_id -- "$completion_command\r"

	set result incomplete
	set captured {}
	expect -i $qemu_spawn_id \
		-re $success_pattern {
			set result pass
			if {[info exists expect_out(buffer)]} {
				lappend captured $expect_out(buffer)
			}
		} \
		-re $failure_pattern {
			set result fail
			if {[info exists expect_out(buffer)]} {
				lappend captured $expect_out(buffer)
			}
		} \
		timeout {set result incomplete} \
		eof {
			puts stderr "STAGE_RESULT name={$description} result=INCOMPLETE reason=qemu-eof"
			puts stderr "QEMU exited during $description."
			exit 1
		}

	# Consume all command output, including guest STAGE_RESULT lines, before
	# waiting for the interactive prompt. A stage marker cannot masquerade as
	# prompt text and leave later output to contaminate the next command.
	expect -i $qemu_spawn_id -exact $completion_marker {
		if {[info exists expect_out(buffer)]} {
			lappend captured $expect_out(buffer)
		}
	} timeout {
		set result incomplete
		puts stderr "The serial command completion marker was not returned after $description."
		exit 1
	} eof {
		puts stderr "QEMU exited before the serial command completion marker for $description."
		exit 1
	}
	expect -i $qemu_spawn_id -re {SEYRIX-ROOT#[ \t]*} {} timeout {
		puts stderr "The serial root shell did not return after $description."
		exit 1
	}

	set duration_ms [expr {[clock milliseconds] - $started_ms}]
	if {$result eq "pass"} {
		puts "STAGE_RESULT name={$description} result=PASS duration_ms=$duration_ms"
	} elseif {$result eq "fail"} {
		puts stderr "STAGE_RESULT name={$description} result=FAIL duration_ms=$duration_ms"
		incr ::acceptance_failures
	} else {
		puts stderr "STAGE_RESULT name={$description} result=INCOMPLETE reason=timeout duration_ms=$duration_ms timeout_s=$timeout"
		incr ::acceptance_failures
	}
	set timeout $saved_timeout
	return [list $result {*}$captured]
}

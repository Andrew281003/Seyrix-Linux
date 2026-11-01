#!/usr/bin/env tclsh

foreach path $argv {
	set channel [open $path r]
	set source [read $channel]
	close $channel
	if {![info complete $source]} {
		puts stderr "TCL_INCOMPLETE=$path"
		exit 1
	}
}
puts "TCL_SOURCE_COMPLETENESS=PASS"

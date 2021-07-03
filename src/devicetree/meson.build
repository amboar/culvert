dtc_prog = find_program('dtc')
ld_prog = find_program('ld')

# TODO: Figure out how to use a generator
g4_dtb = custom_target('g4_dtb',
		input: [ 'g4.dts' ],
		output: [ 'g4.dtb' ],
		command: [ dtc_prog, '@INPUT@', '-O', 'dtb', '-o', '@OUTPUT@' ])

g4_dtbo = custom_target('g4_dtbo',
		input: g4_dtb,
		output: [ 'g4.dtb.o' ],
		command: [ ld_prog, '-r', '-b', 'binary', '@INPUT@', '-o', '@OUTPUT@'])

g5_dtb = custom_target('g5_dtb',
		input: [ 'g5.dts' ],
		output: [ 'g5.dtb' ],
		command: [ dtc_prog, '@INPUT@', '-O', 'dtb', '-o', '@OUTPUT@' ])

g5_dtbo = custom_target('g5_dtbo',
		input: g5_dtb,
		output: [ 'g5.dtb.o' ],
		command: [ ld_prog, '-r', '-b', 'binary', '@INPUT@', '-o', '@OUTPUT@'])

g6_dtb = custom_target('g6_dtb',
		input: [ 'g6.dts' ],
		output: [ 'g6.dtb' ],
		command: [ dtc_prog, '@INPUT@', '-O', 'dtb', '-o', '@OUTPUT@' ])

g6_dtbo = custom_target('g6_dtbo',
		input: g6_dtb,
		output: [ 'g6.dtb.o' ],
		command: [ ld_prog, '-r', '-b', 'binary', '@INPUT@', '-o', '@OUTPUT@'])

dtbos = [ g4_dtbo, g5_dtbo, g6_dtbo ]

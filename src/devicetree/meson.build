dtc_prog = find_program('dtc')
ld_prog = find_program('ld')

cc_prog = meson.get_compiler('c').cmd_array()

# TODO: Figure out how to use a generator

cpp_cmd = [ cc_prog, '-E',
            '-x', 'assembler-with-cpp',
            '-nostdinc',
            '-I', '@SOURCE_ROOT@' / 'src',
            '-o', '@OUTPUT@',
            '@INPUT@' ]
dtc_cmd = [ dtc_prog, '@INPUT@', '-O', 'dtb', '-o', '@OUTPUT@' ]
ld_cmd = [ ld_prog, '-r', '-b', 'binary', '@INPUT@', '-o', '@OUTPUT@']

g4_dts_i = custom_target('g4_dts',
		input: 'g4.dts',
		output: 'g4.dts.i',
		command: cpp_cmd)

g4_dtb = custom_target('g4_dtb',
		input: g4_dts_i,
		output: 'g4.dtb',
		command: dtc_cmd)

g4_dtbo = custom_target('g4_dtbo',
		input: g4_dtb,
		output: 'g4.dtb.o',
		command: ld_cmd)

g5_dts_i = custom_target('g5_dts',
		input: 'g5.dts',
		output: 'g5.dts.i',
		command: cpp_cmd)

g5_dtb = custom_target('g5_dtb',
		input: g5_dts_i,
		output: 'g5.dtb',
		command: dtc_cmd)

g5_dtbo = custom_target('g5_dtbo',
		input: g5_dtb,
		output: 'g5.dtb.o',
		command: ld_cmd)

g6_dts_i = custom_target('g6_dts',
		input: 'g6.dts',
		output: 'g6.dts.i',
		command: cpp_cmd)

g6_dtb = custom_target('g6_dtb',
		input: g6_dts_i,
		output: 'g6.dtb',
		command: dtc_cmd)

g6_dtbo = custom_target('g6_dtbo',
		input: g6_dtb,
		output: 'g6.dtb.o',
		command: ld_cmd)

dtbos = [ g4_dtbo, g5_dtbo, g6_dtbo ]

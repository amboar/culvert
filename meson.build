project('culvert', 'c',
	default_options: [
		'buildtype=debugoptimized',
		'default_library=static',
		'warning_level=2',
		'c_std=gnu99',
		'b_lto=true',
		'b_ndebug=true',
		],
	meson_version: '>=0.53.0',
	version: 'v0.4.0')

libfdt_dep = dependency('libfdt',
			default_options: [ 'warning_level=1', ],
			fallback: [ 'dtc', 'libfdt_dep' ],
			required: true)

libfdt_native_dep = dependency('dtc',
		     native: true,
		     default_options: [ 'tools=enable' ],
		     fallback: [ 'dtc', 'libfdt_dep' ],
		     required: true)

subdir('src')

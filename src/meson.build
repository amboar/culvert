fs = import('fs')

if get_option('prefer_static')
	add_project_link_arguments('-static', language: 'c')
endif

incdirs = [ '.' ]

src = files(
	'ahb.c',
	'ast.c',
	'culvert.c',
	'flash.c',
	'host.c',
	'log.c',
	'mmio.c',
	'pci.c',
	'priv.c',
	'prompt.c',
	'rev.c',
	'shell.c',
	'sio.c',
	'soc.c',
	'ts16.c',
	'tty.c',
	'uart/suart.c'
)

conf_data = configuration_data()

# Version
p = run_command('git', 'describe', '--tags', '--dirty', '--always')
version = p.stdout().strip()
conf_data.set_quoted('version', version)

# Architecture-specific capabilities
host = host_machine.cpu_family()
host_inc = 'arch/@0@'.format(host)
if fs.is_dir(host_inc)
	incdirs += [ host_inc ]
endif
incdirs = include_directories(incdirs)

host_lpc = 'arch/@0@/lpc.c'.format(host)
if fs.is_file(host_lpc)
	src += [ host_lpc ]
	conf_data.set10('have_lpc', true)
else
	conf_data.set10('have_lpc', false)
endif

configure_file(input: 'config.h.in',
	       output: 'config.h',
	       configuration: conf_data)

subdir('bridge')
subdir('ccan')
subdir('cmd')
subdir('devicetree')
subdir('soc')

executable('culvert', src, dtbos,
	   include_directories: incdirs,
	   dependencies: [ libfdt_dep ],
	   link_with: [ libccan ],
	   install: true)

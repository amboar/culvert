fs = import('fs')

incdirs = [ '.' ]

src = files(
	'ahb.c',
	'ast.c',
	'cmd/console.c',
	'cmd/debug.c',
	'cmd/devmem.c',
	'cmd/ilpc.c',
	'cmd/otp.c',
	'cmd/p2a.c',
	'cmd/probe.c',
	'cmd/read.c',
	'cmd/replace.c',
	'cmd/reset.c',
	'cmd/sfc.c',
	'cmd/trace.c',
	'cmd/write.c',
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
subdir('devicetree')
subdir('soc')

executable('culvert', src, dtbos,
	   include_directories: incdirs,
	   dependencies: [ libfdt_dep ],
	   link_with: [ libccan ],
	   install: true)
